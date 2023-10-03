/****************************************************************************************
** Copyright (c) 2019 - 2023 Jolla Ltd.
** Copyright (c) 2019 Open Mobile Platform LLC.
**
** All rights reserved.
**
** This file is part of Sailfish Device Encryption package.
**
** You may use this file under the terms of BSD license as follows:
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
**    list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
****************************************************************************************/

#include <errno.h>
#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <usb-moded/usb_moded-modes.h>
#include <usb-moded/usb_moded-dbus.h>
#include <sailfishaccesscontrol/sailfishaccesscontrol.h>

#include "manage.h"

#define DEVICE_OWNER_LOCALE \
    "/home/.system/var/lib/environment/100000/locale.conf"

typedef void (*job_handler)(GDBusProxy *proxy, guint32 id, gpointer user_data);

typedef struct {
    job_handler handler;
    guint32 id;
    gchar result[20];
} job_watch;

typedef enum {
    END_OF_MANAGE_TASKS,
    STOP_UNIT,
    START_UNIT,
    RELOAD_UNITS,
    ENABLE_UNIT,
    MASK_UNIT,
    UNMASK_UNIT,
    CREATE_MARKER,
    REMOVE_MARKER,
} manage_action;

typedef struct {
    manage_action action;
    const char *argument;
} manage_task;

const manage_task finalization_tasks[] = {
    { UNMASK_UNIT, "home.mount" },
    { RELOAD_UNITS, NULL },
    { ENABLE_UNIT, "jolla-actdead-charging.service" },
    { START_UNIT, "home.mount" },
    { STOP_UNIT, "home-encryption-preparation.service" },
    { START_UNIT, "default.target" },
    { END_OF_MANAGE_TASKS }
};

const manage_task preparation_tasks[] = {
    { RELOAD_UNITS, NULL },
    { CREATE_MARKER, "/var/lib/sailfish-device-encryption/encrypt-home" },
    { START_UNIT, "home-encryption-preparation.service" },
    { MASK_UNIT, "home.mount" },
    { RELOAD_UNITS, NULL },
    { START_UNIT, "default.target" },
    { END_OF_MANAGE_TASKS }
};

const manage_task restoration_tasks[] = {
    { UNMASK_UNIT, "home.mount" },
    { RELOAD_UNITS, NULL },
    { REMOVE_MARKER, "/var/lib/sailfish-device-encryption/encrypt-home" },
    { START_UNIT, "home.mount" },
    { STOP_UNIT, "home-encryption-preparation.service" },
    { START_UNIT, "default.target" },
    { END_OF_MANAGE_TASKS }
};

typedef struct {
    GMainLoop *main_loop;
    GDBusConnection *connection;
    GDBusProxy *systemd_manager;
    GDBusProxy *login_manager;
    GDBusProxy *usb_moded;
    gulong signal_handler;
    GList *job_watches;
    guint task;
    const manage_task *tasks;
    gchar *orig_usb_mode;
    guint uid;
} manage_data;

static manage_data *private_data = NULL;

static inline void manage_data_free(manage_data *data)
{
    g_main_loop_unref(data->main_loop);
    g_object_unref(data->connection);
    g_object_unref(data->systemd_manager);
    g_object_unref(data->login_manager);
    g_object_unref(data->usb_moded);
    g_list_free_full(data->job_watches, g_free);
    g_free(data->orig_usb_mode);
    g_free(data);
}

static void add_job_watch(
        manage_data *data,
        job_handler handler,
        guint32 id,
        gchar *result)
{
    job_watch *watch = g_new0(job_watch, 1);
    watch->handler = handler;
    watch->id = id;
    g_assert(strlen(result) < sizeof(watch->result));
    strncpy(watch->result, result, sizeof(watch->result));
    data->job_watches = g_list_append(data->job_watches, watch);
}

static void on_signal_from_systemd(
        GDBusProxy *proxy,
        gchar *sender,
        gchar *name,
        GVariant *parameters,
        gpointer user_data)
{
    manage_data *data = user_data;
    guint32 id;
    gchar *path;
    gchar *result;
    gchar *unit;
    GList *w;
    job_watch *watch;

    if (strcmp(name, "JobRemoved") != 0)
        return;

    g_variant_get(parameters, "(uoss)", &id, &path, &unit, &result);
    w = data->job_watches;
    while (w != NULL) {
        watch = w->data;
        if (watch->id == id && strcmp(result, watch->result) == 0) {
            watch->handler(proxy, id, user_data);
            data->job_watches = g_list_delete_link(data->job_watches, w);
            break;
        }
        w = w->next;
    }
    g_free(path);
    g_free(unit);
    g_free(result);
}

static inline guint get_job_id(GVariant *job)
{
    unsigned int id;
    gchar *path;

    g_variant_get(job, "(o)", &path);

    if (!sscanf(path, "/org/freedesktop/systemd1/job/%u", &id))
        g_assert_not_reached();

    g_free(path);
    return id;
}

static const gchar *get_unit_action(manage_action action)
{
    switch (action) {
        case STOP_UNIT:
            return "StopUnit";
        case START_UNIT:
            return "StartUnit";
        case RELOAD_UNITS:
            return "Reload";
        case ENABLE_UNIT:
            return "EnableUnitFiles";
        case MASK_UNIT:
            return "MaskUnitFiles";
        case UNMASK_UNIT:
            return "UnmaskUnitFiles";
        case CREATE_MARKER:
        case REMOVE_MARKER:
        case END_OF_MANAGE_TASKS:
            break;
    }
    return "";
}

static GVariant *get_unit_arguments(manage_task task)
{
    GVariantBuilder builder;

    switch (task.action) {
        case START_UNIT:
        case STOP_UNIT:
            return g_variant_new("(ss)", task.argument, "replace");
        case RELOAD_UNITS:
            return NULL;
        case ENABLE_UNIT:
            g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
            g_variant_builder_add(&builder, "s", task.argument);
            return g_variant_new("(asbb)", &builder, FALSE, FALSE);
        case MASK_UNIT:
            g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
            g_variant_builder_add(&builder, "s", task.argument);
            // runtime: true, masking is removed on reboot
            return g_variant_new("(asbb)", &builder, TRUE, FALSE);
        case UNMASK_UNIT:
            g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
            g_variant_builder_add(&builder, "s", task.argument);
            return g_variant_new("(asb)", &builder, TRUE);
        case CREATE_MARKER:
        case REMOVE_MARKER:
        case END_OF_MANAGE_TASKS:
            break;
    }
    return NULL;
}

static void handle_next_unit_task(manage_data *data);

static void unit_changed_state(
        GDBusProxy *proxy,
        guint32 id,
        gpointer user_data)
{
    manage_data *data = user_data;

    (void)proxy;
    (void)id;
    handle_next_unit_task(data);
}

static void unit_changing_state(
        GObject *proxy,
        GAsyncResult *res,
        gpointer user_data)
{
    manage_data *data = user_data;
    GError *error = NULL;
    GVariant *job;

    job = g_dbus_proxy_call_finish((GDBusProxy *)proxy, res, &error);
    if (job == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_main_loop_quit(data->main_loop);
        manage_data_free(data);
        g_error_free(error);
        return;
    }

    if (g_variant_is_of_type(job, G_VARIANT_TYPE("(o)")))
        add_job_watch(data, unit_changed_state, get_job_id(job), "done");
    else
        handle_next_unit_task(data);

    g_variant_unref(job);
}

static void create_file(const char *path)
{
    FILE *file;
    printf("Creating file %s\n", path);
    file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "Failed to create file %s\n", path);
    } else {
        fclose(file);
    }
}

static void remove_file(const char *path)
{
    printf("Removing file %s\n", path);
    if (unlink(path) < 0)
        fprintf(stderr, "Failed to remove file %s\n", path);
}

static void handle_next_unit_task(manage_data *data)
{
    manage_task task = data->tasks[data->task];

    switch (task.action) {
        case START_UNIT:
        case STOP_UNIT:
        case RELOAD_UNITS:
        case ENABLE_UNIT:
        case MASK_UNIT:
        case UNMASK_UNIT:
            g_dbus_proxy_call(
                    data->systemd_manager, get_unit_action(task.action),
                    get_unit_arguments(task), G_DBUS_CALL_FLAGS_NONE, -1,
                    NULL, unit_changing_state, data);
            break;
        case CREATE_MARKER:
            create_file(task.argument);
            break;
        case REMOVE_MARKER:
            remove_file(task.argument);
            break;
        case END_OF_MANAGE_TASKS:
            g_signal_handler_disconnect(
                    data->systemd_manager, data->signal_handler);
            if (data->tasks == preparation_tasks) {
                // Stay waiting for BeginEncryption
                printf("Preparation done.\n");
                data->tasks = NULL;
                data->task = 0;
            } else {
                if (data->orig_usb_mode) {
                    // Restore USB mode back to the original
                    g_dbus_proxy_call_sync(
                            data->usb_moded, USB_MODE_STATE_SET,
                            g_variant_new("(s)", data->orig_usb_mode),
                            G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                            NULL);
                }
                g_main_loop_quit(data->main_loop);
                manage_data_free(data);
            }
            return;
    }

    data->task++;

    if (task.action == CREATE_MARKER || task.action == REMOVE_MARKER)
        handle_next_unit_task(data);
}

static void on_signal_from_logind(
        GDBusProxy *proxy,
        gchar *sender,
        gchar *name,
        GVariant *parameters,
        gpointer user_data)
{
    manage_data *data = user_data;
    guint32 id;

    if (strcmp(name, "UserRemoved") != 0)
        return;

    g_variant_get(parameters, "(uo)", &id, NULL);

    if (id != data->uid)
        return;

    g_signal_handler_disconnect(data->login_manager, data->signal_handler);
    data->signal_handler = g_signal_connect(
            data->systemd_manager, "g-signal",
            G_CALLBACK(on_signal_from_systemd), data);
    handle_next_unit_task(data);
}

static void terminate_user(manage_data *data)
{
    data->uid = sailfish_access_control_systemuser_uid();
    if (data->uid == SAILFISH_UNDEFINED_UID) {
        fprintf(stderr, "Unable to detect system user\n");
        g_main_loop_quit(data->main_loop);
        manage_data_free(data);
        return;
    }

    data->signal_handler = g_signal_connect(
            data->login_manager, "g-signal",
            G_CALLBACK(on_signal_from_logind), data);

    g_dbus_proxy_call(
            data->login_manager, "TerminateUser",
            g_variant_new("(u)", data->uid),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL,
            NULL, NULL);
}

static void got_set_mode(
        GObject *proxy,
        GAsyncResult *res,
        gpointer user_data)
{
    manage_data *data = user_data;
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_proxy_call_finish((GDBusProxy *)proxy, res, &error);
    if (result == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        g_free(data->orig_usb_mode);
        data->orig_usb_mode = NULL;
    } else {
        g_variant_unref(result);
    }

    // Continue with the encryption
    terminate_user(data);
}

static void got_mode_request(
        GObject *proxy,
        GAsyncResult *res,
        gpointer user_data)
{
    manage_data *data = user_data;
    GError *error = NULL;
    GVariant *result;
    gchar *mode;

    result = g_dbus_proxy_call_finish((GDBusProxy *)proxy, res, &error);
    if (result == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        // Continue with the encryption
        terminate_user(data);
        return;
    }
    g_variant_get(result, "(s)", &mode);
    g_variant_unref(result);

    // Change mode if not charging or developer mode
    if (strcmp(mode, MODE_CHARGING) && strcmp(mode, MODE_DEVELOPER)) {
        data->orig_usb_mode = mode;
        g_dbus_proxy_call(
            data->usb_moded, USB_MODE_STATE_SET,
            g_variant_new("(s)", MODE_CHARGING),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL,
            got_set_mode, data);
    } else {
        g_free(mode);
        // Continue with the encryption
        terminate_user(data);
    }
}

static void got_usb_moded(
        GObject *proxy,
        GAsyncResult *res,
        gpointer user_data)
{
    manage_data *data = user_data;
    GError *error = NULL;

    data->usb_moded = g_dbus_proxy_new_finish(res, &error);
    if (data->usb_moded == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        // Continue with the encryption
        terminate_user(data);
    } else {
        g_dbus_proxy_call(
                data->usb_moded, USB_MODE_TARGET_STATE_GET,
                NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                got_mode_request, user_data);
    }
}

static void got_subscription(
        GObject *proxy,
        GAsyncResult *res,
        gpointer user_data)
{
    manage_data *data = user_data;
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_proxy_call_finish((GDBusProxy *)proxy, res, &error);
    if (result == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_main_loop_quit(data->main_loop);
        manage_data_free(data);
        g_error_free(error);
        return;
    }
    g_variant_unref(result);

    // Acquire USB daemon proxy to check its state
    g_dbus_proxy_new(
            data->connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "com.meego.usb_moded",
            "/com/meego/usb_moded",
            "com.meego.usb_moded",
            NULL, got_usb_moded, user_data);
}

static void got_login_manager(
        GObject *proxy,
        GAsyncResult *res,
        gpointer user_data)
{
    manage_data *data = user_data;
    GError *error = NULL;

    data->login_manager = g_dbus_proxy_new_finish(res, &error);
    if (data->login_manager == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_main_loop_quit(data->main_loop);
        manage_data_free(data);
        g_error_free(error);
        return;
    }

    g_dbus_proxy_call(
            data->systemd_manager, "Subscribe",
            NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
            got_subscription, user_data);
}

static void got_systemd_manager(
        GObject *proxy,
        GAsyncResult *res,
        gpointer user_data)
{
    manage_data *data = user_data;
    GError *error = NULL;

    data->systemd_manager = g_dbus_proxy_new_finish(res, &error);
    if (data->systemd_manager == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_main_loop_quit(data->main_loop);
        manage_data_free(data);
        g_error_free(error);
        return;
    }

    g_dbus_proxy_new(
            data->connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            NULL, got_login_manager, user_data);
}

static void got_bus(GObject *proxy, GAsyncResult *res, gpointer user_data)
{
    manage_data *data = user_data;
    GError *error = NULL;

    data->connection = g_bus_get_finish(res, &error);
    if (data->connection == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_main_loop_quit(data->main_loop);
        manage_data_free(data);
        g_error_free(error);
        return;
    }

    // Systemd proxy
    g_dbus_proxy_new(
            data->connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager",
            NULL, got_systemd_manager, user_data);
}

static inline void cleanup_home_dir()
{
    // Clean up locale file from home directory
    char *path = strdup(DEVICE_OWNER_LOCALE);
    errno = 0;
    if (unlink(path) < 0 && errno != ENOENT) {
        fprintf(stderr, "Failed to remove file %s: %s\n",
                path, strerror(errno));
    } else {
        *(strrchr(path, '/')) = '\0';
        while (strlen(path) > ((sizeof "/home/")-1)) {
            errno = 0;
            if (rmdir(path) < 0) {
                if (errno != ENOTEMPTY && errno != ENOENT) {
                    fprintf(stderr, "Failed to remove directory %s: %s\n",
                            path, strerror(errno));
                }
                break;
            }
            *(strrchr(path, '/')) = '\0';
        };
    }

    free(path);
}

gboolean finalize(GMainLoop *main_loop, gboolean restore)
{
    printf("Restarting user session with encrypted home.\n");
    if (private_data) {
        if (private_data->tasks != NULL)
            return FALSE; // Finalize or prepare is already in progress
    } else {
        private_data = g_new0(manage_data, 1);
        private_data->main_loop = g_main_loop_ref(main_loop);
    }

    cleanup_home_dir();

    if (restore)
        private_data->tasks = restoration_tasks;
    else
        private_data->tasks = finalization_tasks;

    if (private_data->connection == NULL) {
        g_bus_get(G_BUS_TYPE_SYSTEM, NULL, got_bus, private_data);
    } else {
        // Already prepared, skip initialisation
        terminate_user(private_data);
    }

    return TRUE;
}

void prepare(GMainLoop *main_loop)
{
    g_assert(private_data == NULL);  // It's an error to call this twice

    private_data = g_new0(manage_data, 1);
    private_data->main_loop = g_main_loop_ref(main_loop);
    private_data->tasks = preparation_tasks;
    printf("Preparing encrypted home.\n");
    g_bus_get(G_BUS_TYPE_SYSTEM, NULL, got_bus, private_data);
}

// vim: expandtab:ts=4:sw=4
