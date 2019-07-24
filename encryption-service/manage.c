/*
 * Manage user session.
 *
 * Copyright (c) 2019 Jolla Ltd.
 */

#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "manage.h"

#define NEMO_UID 100000

typedef void (*job_handler)(GDBusProxy *proxy, guint32 id, gpointer user_data);

typedef struct {
    job_handler handler;
    guint32 id;
    gchar result[20];
} job_watch;

typedef struct {
    GMainLoop *main_loop;
    GDBusConnection *connection;
    GDBusProxy *systemd_manager;
    GDBusProxy *login_manager;
    gulong signal_handler;
    GList *job_watches;
    guint task;
    gboolean preparing;
} manage_data;

static manage_data *private_data = NULL;

static inline void manage_data_free(manage_data *data)
{
    g_object_unref(data->connection);
    g_object_unref(data->systemd_manager);
    g_object_unref(data->login_manager);
    g_list_free_full(data->job_watches, g_free);
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
}

static inline guint get_job_id(GVariant *job)
{
    unsigned int id;
    gchar *path;

    g_variant_get(job, "(o)", &path);

    if (!sscanf(path, "/org/freedesktop/systemd1/job/%u", &id))
        g_assert_not_reached();

    return id;
}

enum unit_action {
    END_OF_UNIT_TASKS,
    STOP_UNIT,
    START_UNIT,
    RELOAD_UNITS,
    ENABLE_UNIT,
    CREATE_MARKER,
};

typedef struct {
    enum unit_action action;
    char *unit;
} unit_task;

unit_task unit_tasks[] = {
    { RELOAD_UNITS, NULL },
    { ENABLE_UNIT, "jolla-actdead-charging.service" },
    { START_UNIT, "home.mount" },
    { STOP_UNIT, "home-encryption-preparation.service" },
    { START_UNIT, "default.target" },
    { END_OF_UNIT_TASKS }
};

unit_task preparation_tasks[] = {
    { RELOAD_UNITS, NULL },
    { CREATE_MARKER, "/var/lib/sailfish-device-encryption/encrypt-home" },
    { START_UNIT, "home-encryption-preparation.service" },
    { START_UNIT, "default.target" },
    { END_OF_UNIT_TASKS }
};

static gchar * get_unit_action(enum unit_action action)
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
        case END_OF_UNIT_TASKS:
        case CREATE_MARKER:
            break;
    }
    return "";
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

static void handle_next_unit_task(manage_data *data)
{
    GVariantBuilder builder;
    unit_task task;

    if (data->preparing) task = preparation_tasks[data->task];
    else task = unit_tasks[data->task];

    switch (task.action) {
        case START_UNIT:
        case STOP_UNIT:
            g_dbus_proxy_call(
                    data->systemd_manager, get_unit_action(task.action),
                    g_variant_new("(ss)", task.unit, "replace"),
                    G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                    unit_changing_state, data);
            break;
        case RELOAD_UNITS:
            g_dbus_proxy_call(
                    data->systemd_manager, get_unit_action(task.action),
                    NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                    unit_changing_state, data);
            break;
        case ENABLE_UNIT:
            g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
            g_variant_builder_add(&builder, "s", task.unit);
            g_dbus_proxy_call(
                    data->systemd_manager, get_unit_action(task.action),
                    g_variant_new("(asbb)", &builder, FALSE, FALSE),
                    G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                    unit_changing_state, data);
            break;
        case END_OF_UNIT_TASKS:
            g_signal_handler_disconnect(
                    data->systemd_manager, data->signal_handler);
            if (data->preparing) {
                // Stay waiting for BeginEncryption
                printf("Preparation done.\n");
                data->preparing = FALSE;
                data->task = 0;
            } else {
                g_main_loop_quit(data->main_loop);
                manage_data_free(data);
            }
            return;
            break;
        case CREATE_MARKER:
            printf("Creating marker %s\n", task.unit);
            FILE *f=fopen(task.unit, "w");
            if (!f) {
                fprintf(stderr, "Failed to create marker file %s\n", task.unit);
            }
            fclose(f);
            // To the next one
            data->task++;
            handle_next_unit_task(data);
            return;
            break;
    }

    data->task++;
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
    gchar *path;

    if (strcmp(name, "UserRemoved") != 0)
        return;

    g_variant_get(parameters, "(uo)", &id, &path);

    if (id != NEMO_UID)
        return;

    g_signal_handler_disconnect(data->login_manager, data->signal_handler);
    data->signal_handler = g_signal_connect(
            data->systemd_manager, "g-signal",
            G_CALLBACK(on_signal_from_systemd), data);
    handle_next_unit_task(data);
}

static void terminate_user(manage_data *data)
{
    data->signal_handler = g_signal_connect(
            data->login_manager, "g-signal",
            G_CALLBACK(on_signal_from_logind), data);

    g_dbus_proxy_call(
            data->login_manager, "TerminateUser",
            g_variant_new("(u)", NEMO_UID),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL,
            NULL, NULL);
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

    terminate_user(data);
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

    g_dbus_proxy_new(
            data->connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager",
            NULL, got_systemd_manager, user_data);
}

void finalize(GMainLoop *main_loop)
{
    printf("Restarting user session with encrypted home.\n");
    if (!private_data) {
        private_data = g_new0(manage_data, 1);
        private_data->main_loop = main_loop;
        g_bus_get(G_BUS_TYPE_SYSTEM, NULL, got_bus, private_data);
    } else {
        // Already prepared, skip initialisation
        terminate_user(private_data);
    }
}

void prepare(GMainLoop *main_loop)
{
    private_data = g_new0(manage_data, 1);
    private_data->main_loop = main_loop;
    private_data->preparing = TRUE;
    printf("Preparing encrypted home.\n");
    g_bus_get(G_BUS_TYPE_SYSTEM, NULL, got_bus, private_data);
}

// vim: expandtab:ts=4:sw=4
