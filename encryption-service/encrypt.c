/*
 * Encrypting home.
 *
 * Copyright (c) 2019 Jolla Ltd.
 */

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <udisks/udisks.h>
#include "encrypt.h"

// TODO: Make this more dynamic
#ifndef DEVICE_TO_ENCRYPT
#define DEVICE_TO_ENCRYPT /dev/sailfish/home
#endif

#ifndef ENCRYPTION_TYPE
#define ENCRYPTION_TYPE luks1
#endif

#ifndef FILESYSTEM_FORMAT
#define FILESYSTEM_FORMAT ext4
#endif

#define UDISKS_INTERFACE "org.freedesktop.UDisks2"
#define UDISKS_MANAGER_PATH "/org/freedesktop/UDisks2/Manager"

#define QUOTE(s) #s
#define STR(s) QUOTE(s)

// TODO: This probably needs a "supervisor" that
//       watches for interface changes etc. on udisks
//       and starts new phases based on that. It's
//       probably a requirement for unmounting and
//       locking devices properly before encryption.

/*
 * This could be done with GObject signals but I think
 * this is simpler for now. These may be turned into
 * GSimpleAction or some other GObject later and/or
 * combined with the invocation_data struct.
 */
encryption_state status = ENCRYPTION_NOT_STARTED;
encryption_status_changed status_change_callback;

void set_status(encryption_state state)
{
    status = state;
    status_change_callback(state);
}

void init_encryption_service(encryption_status_changed change_callback)
{
    status_change_callback = change_callback;
}

/*
 * Struct to track internal state.
 */
typedef struct {
    GDBusConnection *connection;
    UDisksManager *manager;
    UDisksBlock *block;
    gchar *passphrase;
} invocation_data;

static inline void invocation_data_free(invocation_data *data)
{
    g_object_unref(data->connection);
    g_object_unref(data->manager);
    g_object_unref(data->block);
    g_free(data->passphrase);
    g_free(data);
}

static inline void end_encryption_to_failure(invocation_data *data)
{
    set_status(ENCRYPTION_FAILED);
    invocation_data_free(data);
}

static void rescan_complete(
        GObject *block,
        GAsyncResult *res,
        gpointer user_data)
{
    invocation_data *data = user_data;

    udisks_block_call_rescan_finish((UDisksBlock *)block, res, NULL);

    if (status == ENCRYPTION_NEEDS_RESCAN) {
        set_status(ENCRYPTION_FINISHED);
        printf("Finished encryption successfully.\n");
    }

    invocation_data_free(data);
}

static void format_complete(
        GObject *block,
        GAsyncResult *res,
        gpointer user_data)
{
    GVariant *arguments;
    invocation_data *data = user_data;
    GError *error = NULL;

    if (udisks_block_call_format_finish((UDisksBlock *)block, res, &error)) {
        set_status(ENCRYPTION_NEEDS_RESCAN);
    } else {
        end_encryption_to_failure(data);
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
    }

    arguments = g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);
    udisks_block_call_rescan(
            (UDisksBlock *)block, arguments, NULL,
            rescan_complete, data);
}

static inline void start_format_luks(invocation_data *data)
{
    GVariantBuilder builder, subbuilder;
    GVariant *config_items, *options;

    set_status(ENCRYPTION_IN_PROGRESS);

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
            &builder, "{sv}", "encrypt.passphrase",
            g_variant_new_string(data->passphrase));
    g_variant_builder_add(
            &builder, "{sv}", "encrypt.type",
            g_variant_new_string(STR(ENCRYPTION_TYPE)));
    g_variant_builder_add(
            &builder, "{sv}", "update-partition-type",
            g_variant_new_boolean(TRUE));
    g_variant_builder_add(
            &builder, "{sv}", "no-block", g_variant_new_boolean(TRUE));
    g_variant_builder_add(
            &builder, "{sv}", "dry-run-first", g_variant_new_boolean(TRUE));
    g_variant_builder_add(
            &builder, "{sv}", "tear-down", g_variant_new_boolean(TRUE));

    g_variant_builder_init(&subbuilder, G_VARIANT_TYPE("a(sa{sv})"));
    g_variant_builder_open(&subbuilder, G_VARIANT_TYPE("(sa{sv})"));
    g_variant_builder_add(&subbuilder, "s", "crypttab");
    g_variant_builder_open(&subbuilder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
            &subbuilder, "{sv}", "passphrase-contents",
            g_variant_new_bytestring(""));
    g_variant_builder_add(
            &subbuilder, "{sv}", "options",
            g_variant_new_bytestring("tries=0,x-systemd.device-timeout=0"));
    g_variant_builder_add(
            &subbuilder, "{sv}", "track-parents",
            g_variant_new_boolean(TRUE));
    g_variant_builder_close(&subbuilder);
    g_variant_builder_close(&subbuilder);

    g_variant_builder_open(&subbuilder, G_VARIANT_TYPE("(sa{sv})"));
    g_variant_builder_add(&subbuilder, "s", "fstab");
    g_variant_builder_open(&subbuilder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
            &subbuilder, "{sv}", "dir", g_variant_new_bytestring("/home"));
    g_variant_builder_add(
            &subbuilder, "{sv}", "type",
            g_variant_new_bytestring(STR(FILESYSTEM_FORMAT)));
    g_variant_builder_add(
            &subbuilder, "{sv}", "opts",
            g_variant_new_bytestring(
                "defaults,noatime,x-systemd.device-timeout=0"));
    g_variant_builder_add(
            &subbuilder, "{sv}", "freq", g_variant_new_int32(0));
    g_variant_builder_add(
            &subbuilder, "{sv}", "passno", g_variant_new_int32(0));
    g_variant_builder_add(
            &subbuilder, "{sv}", "track-parents",
            g_variant_new_boolean(TRUE));
    g_variant_builder_close(&subbuilder);
    g_variant_builder_close(&subbuilder);

    config_items = g_variant_builder_end(&subbuilder);
    g_variant_builder_add(&builder, "{sv}", "config-items", config_items);
    options = g_variant_builder_end(&builder);

    udisks_block_call_format(
            data->block, STR(FILESYSTEM_FORMAT), options,
            NULL, format_complete, data);
}

static void can_format_to_type(
        GObject *manager,
        GAsyncResult *res,
        gpointer user_data)
{
    GVariant *avail;
    gboolean available;
    gchar *bin;
    invocation_data *data = user_data;
    GError *error = NULL;

    if (!udisks_manager_call_can_format_finish(
                (UDisksManager *)manager, &avail, res, &error)) {
        fprintf(stderr, "%s\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
        return;
    }

    g_variant_get(avail, "(bs)", &available, &bin);

    if (!available) {
        fprintf(stderr, "%s is not available, needs %s.\n",
                STR(FILESYSTEM_FORMAT), bin);
        end_encryption_to_failure(data);
        return;
    }

    printf("Starting encryption. All data will be destroyed.\n");
    start_format_luks(data);

    return;
}

static void found_block_device(
        GObject *proxy,
        GAsyncResult *res,
        gpointer user_data)
{
    UDisksBlock *block;
    invocation_data *data = user_data;
    GError *error = NULL;

    block = udisks_block_proxy_new_finish(res, &error);
    if (block == NULL) {
        fprintf(stderr, "%s\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
        return;
    }

    data->block = block;

    printf("Selected '%s' for encryption.\n", udisks_block_get_device(block));

    udisks_manager_call_can_format(
            data->manager, STR(FILESYSTEM_FORMAT), NULL,
            can_format_to_type, data);
}

static void check_device_list(
        GObject *manager,
        GAsyncResult *res,
        gpointer user_data)
{
    const gchar *bdev;
    invocation_data *data = user_data;
    gchar **devices;
    GVariant *devnames, *dev;
    GError *error = NULL;
    GVariantIter *iter;
    gsize length;

    if (!udisks_manager_call_resolve_device_finish(
                (UDisksManager *)manager, &devices, res, &error)) {
        fprintf(stderr, "%s\n", error->message);
        end_encryption_to_failure(data);
        return;
    }

    devnames = g_variant_new_objv((const gchar * const *)devices, -1);
    iter = g_variant_iter_new(devnames);

    length = g_variant_iter_n_children(iter);
    if (length > 1)
        fprintf(stderr,
                "Warning: Multiple block devices found, using first.\n");

    if (length < 1) {
        fprintf(stderr, "No device found. Aborting.\n");
        end_encryption_to_failure(data);
        g_variant_iter_free(iter);
        g_free(devices);
        g_variant_unref(devnames);
        return;
    }

    dev = g_variant_iter_next_value(iter);
    bdev = g_variant_get_string(dev, &length);

    udisks_block_proxy_new(
            data->connection, G_DBUS_PROXY_FLAGS_NONE,
            UDISKS_INTERFACE, bdev, NULL, found_block_device, data);

    g_variant_unref(dev);
    g_variant_iter_free(iter);
    g_free(devices);
    g_variant_unref(devnames);
    return;
}

static inline void find_device_to_encrypt(invocation_data *data_)
{
    GVariantBuilder builder;
    invocation_data *data = (invocation_data *)data_;
    GVariant *devnames, *arguments;

    g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);
    g_variant_builder_add(&builder, "{sv}", "path",
        g_variant_new_string(STR(DEVICE_TO_ENCRYPT)));
    devnames = g_variant_builder_end(&builder);
    arguments = g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);

    udisks_manager_call_resolve_device(
            data->manager, devnames, arguments, NULL,
            check_device_list, data);
}

static void got_manager(GObject *proxy, GAsyncResult *res, gpointer user_data)
{
    invocation_data *data = user_data;
    GError *error = NULL;

    data->manager = udisks_manager_proxy_new_finish(res, &error);
    if (data->manager == NULL) {
        fprintf(stderr, "%s\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
        return;
    }

    find_device_to_encrypt(data);
}

static void got_bus(GObject *proxy, GAsyncResult *res, gpointer user_data)
{
    invocation_data *data = user_data;
    GError *error = NULL;

    data->connection = g_bus_get_finish(res, &error);
    if (data->connection == NULL) {
        fprintf(stderr, "%s\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
        return;
    }

    udisks_manager_proxy_new(
            data->connection, G_DBUS_PROXY_FLAGS_NONE, UDISKS_INTERFACE,
            UDISKS_MANAGER_PATH, NULL, got_manager, data);
}

gboolean start_to_encrypt(gchar *passphrase)
{
    invocation_data *data;

    if (status != ENCRYPTION_NOT_STARTED)
        return FALSE;

    set_status(ENCRYPTION_IN_PREPARATION);
    data = g_new0(invocation_data, 1);
    data->passphrase = passphrase;
    g_bus_get(G_BUS_TYPE_SYSTEM, NULL, got_bus, data);
    return TRUE;
}

encryption_state get_encryption_status(void)
{
    return status;
}

// vim: expandtab:ts=4:sw=4
