/*
 * Copyright (c) 2021 Jolla Ltd.
 *
 * License: Proprietary
 */

#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <errno.h>
#include <unistd.h>
#include "homecopy.h"
#define COPY_SERVICE "home-encryption-copy.service"
#define RESTORE_SERVICE "home-restore.service"

typedef struct {
    GMainLoop *main_loop;
    GDBusConnection *connection;
    GDBusProxy *systemd_manager;
    int inotify_fd;
    void (*signal_emitter)(copy_result res);
} manage_data;

static copy_state state = NOT_COPYING;
static char service[50];

static manage_data *private_data = NULL;
static const char *copy_done_file = "/tmp/.sailfish_copy_done";
static const char *copy_conf_file = "/var/lib/sailfish-device-encryption/home_copy.conf";

void set_copy_done()
{
    state = NOT_COPYING;
}

static void free_manage_data()
{
    g_main_loop_unref(private_data->main_loop);
    g_object_unref(private_data->connection);
    g_object_unref(private_data->systemd_manager);
    g_free(private_data);
}

static void exit_with_error(GError *error)
{
    if (error != NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
    }
    private_data->signal_emitter(COPY_PROCESS_ERROR);
    set_copy_done();
    g_main_loop_quit(private_data->main_loop);
    free_manage_data();
}

copy_result wait_for_copy()
{
    char _Alignas(struct inotify_event) buf[4096];
    const struct inotify_event *event = NULL;
    ssize_t len = 0;

    while (len = read(private_data->inotify_fd, buf, sizeof buf), len > 0) {
        if (len == -1 && errno != EAGAIN) {
            fprintf(stderr, "inotify read failed\n");
            return COPY_PROCESS_ERROR;
        }
        for (char *ptr = buf; ptr < buf + len;
                ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *) ptr;
            if (event->mask & IN_CLOSE_WRITE) {
                FILE *f = fopen(copy_done_file, "r");
                int copy_res = 1;
                if (fscanf(f, "%d", &copy_res)) {
                    fclose(f);
                    return (copy_res == 0) ? COPY_SUCCESS : COPY_FAILED;
                }
            }
        }
    }
    return COPY_PROCESS_ERROR;
}

// a marker file is used to track the status of the copy operation.
static void init_watcher()
{
    FILE *file = fopen(copy_done_file, "w");
    if (file == NULL) {
        exit_with_error(NULL);
        return;
    }
    fprintf(file, "%d", 1);
    fclose(file);
    private_data->inotify_fd = inotify_init();
    inotify_add_watch(private_data->inotify_fd, copy_done_file, IN_CLOSE_WRITE);
}

static void started_copy_service(GObject *proxy, GAsyncResult *res, gpointer data)
{
    GError *error = NULL;
    GVariant *result = g_dbus_proxy_call_finish((GDBusProxy *)proxy, res, &error);
    if (result == NULL) {
        exit_with_error(error);
        return;
    }
    copy_result copy_res = wait_for_copy();
    private_data->signal_emitter(copy_res);
    set_copy_done();
    g_main_loop_quit(private_data->main_loop);
    free_manage_data();
    g_variant_unref(result);
}

static void got_subscription(GObject *proxy, GAsyncResult *res, gpointer data)
{
    GError *error = NULL;
    GVariant *result = g_dbus_proxy_call_finish((GDBusProxy *)proxy, res, &error);
    if (result == NULL) {
        exit_with_error(error);
        return;
    }
    init_watcher();

    g_dbus_proxy_call(private_data->systemd_manager, "StartUnit",
                      g_variant_new("(ss)", service, "replace"),
                      G_DBUS_CALL_FLAGS_NONE, -1, NULL, started_copy_service, &error);
    g_variant_unref(result);
}

static void got_systemd_manager(GObject *proxy, GAsyncResult *res, gpointer data)
{
    GError *error = NULL;
    private_data->systemd_manager = g_dbus_proxy_new_finish(res, &error);

    if (private_data->systemd_manager == NULL) {
        exit_with_error(error);
        return;
    }
    g_dbus_proxy_call(private_data->systemd_manager, "Subscribe",
                      NULL, G_DBUS_CALL_FLAGS_NONE, -1,
                      NULL, got_subscription, NULL);
}

// If path is empty remove copy_conf_file
gboolean set_copy_location(gchar *path)
{
    if (!strcmp(path, "")) {
        int ret = remove(copy_conf_file);
        if (ret)
            fprintf(stderr, "Could not remove file %s: %m\n", copy_conf_file);
        return !ret;
    }
    printf("Setting copy location to %s \n", path);
    FILE *f = NULL;
    f = fopen(copy_conf_file, "w");
    if (f == NULL) {
        return FALSE;
    } else if (fprintf(f ,"%s\n", path) >= 0) {
        fclose(f);
        return TRUE;
    }
    fclose(f);
    return FALSE;
}

static void got_bus(GObject *proxy, GAsyncResult *res, gpointer data)
{
    GError *error = NULL;
    private_data->connection = g_bus_get_finish(res, &error);
    if (private_data->connection == NULL) {
        exit_with_error(error);
        return;
    }
    g_dbus_proxy_new(private_data->connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
                     "org.freedesktop.systemd1",
                     "/org/freedesktop/systemd1",
                     "org.freedesktop.systemd1.Manager",
                     NULL, got_systemd_manager, NULL);
}

copy_state get_copy_state()
{
    return state;
}

void copy(GMainLoop *main_loop, void (*emit_signal)(copy_result res))
{
    state = COPYING;
    if (private_data == NULL)
        private_data = g_new0(manage_data, 1);
    private_data->main_loop = main_loop;
    private_data->signal_emitter = emit_signal;
    g_bus_get(G_BUS_TYPE_SYSTEM, NULL, got_bus, NULL);
}

void copy_home(GMainLoop *main_loop, void (*emit_signal)(copy_result res))
{
    printf("Starting home copy\n");
    strcpy(service, COPY_SERVICE);
    copy(main_loop, emit_signal);
}

void restore_home(GMainLoop *main_loop, void (*emit_signal)(copy_result res))
{
    printf("Starting home restoration\n");
    strcpy(service, RESTORE_SERVICE);
    copy(main_loop, emit_signal);
}
