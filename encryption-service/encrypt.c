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
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <udisks/udisks.h>
#include <unistd.h>
#include "encrypt.h"

#define BLOCK_SIZE 4096
#define KEY_SIZE 16

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

#ifndef SYSTEMD_UNIT_DIR
#define SYSTEMD_UNIT_DIR /etc/systemd/system/
#endif

#define TEMPORARY_KEY_FILE \
    "/var/lib/sailfish-device-encryption/temporary-encryption-key"
#define UPDATE_KEY_FILE \
    "/var/lib/sailfish-device-encryption/update-encryption-key"

#define QUOTE(s) #s
#define STR(s) QUOTE(s)

const char *unit_dir = STR(SYSTEMD_UNIT_DIR);

const char *home_conf_name = "50-home.conf";
const char *home_conf_content = "[Unit]\nRequiresMountsFor=/home\n";

const char *settle_conf_name = "50-settle.conf";
const char *settle_conf_content = "[Unit]\n" \
    "After=home-mount-settle.service\n" \
    "Requires=home-mount-settle.service\n";

const char *device_conf_name = "50-sailfish-home.conf";
const char *device_conf_template = "[Unit]\n" \
    "After=dev-disk-by\\x2duuid-%s.device\n";

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
    UDisksClient *client;
    UDisksManager *manager;
    GDBusObjectManager *object_manager;
    UDisksBlock *block;
    gchar *passphrase;
    gboolean passphrase_is_temporary;
    erase_t erase;
    gchar *crypto_device_path;
    gchar *cleartext_device_uuid;
    gulong signal_handler;
} invocation_data;

static inline void invocation_data_free(invocation_data *data)
{
    if (data == NULL)
        return;
    g_object_unref(data->connection);
    g_object_unref(data->manager);
    g_object_unref(data->block);
    g_free(data->passphrase);
    g_free(data->crypto_device_path);
    g_free(data->cleartext_device_uuid);
    g_free(data);
}

static inline void end_encryption_to_failure(invocation_data *data)
{
    set_status(ENCRYPTION_FAILED);
    g_signal_handler_disconnect(data->object_manager, data->signal_handler);
    invocation_data_free(data);
}

static inline gboolean set_unit_conf(
        const char *unit,
        const char *conf_name,
        const char *content)
{
    int file;
    size_t length = strlen(unit_dir) + strlen(unit) + strlen(conf_name) + 5;
    char *path = malloc(length);
    if (path == NULL) {
        fprintf(stderr, "Could not malloc memory for path. Aborting.\n");
        return FALSE;
    }

    strncpy(path, unit_dir, length);
    strncat(path, "/", length);
    strncat(path, unit, length);
    strncat(path, ".d", length);

    errno = 0;
    if (mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0 &&
            errno != EEXIST) {
        fprintf(stderr, "Could not create directory %s: %s. Aborting.\n", path,
                strerror(errno));
        free(path);
        return FALSE;
    }

    strncat(path, "/", length);
    strncat(path, conf_name, length);

    file = open(
            path, O_TRUNC | O_CREAT | O_WRONLY,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (file == -1) {
        fprintf(stderr, "Could not create file %s: %s. Aborting.\n", path,
                strerror(errno));
        free(path);
        return FALSE;
    }

    if (write(file, content, strlen(content)) < strlen(content)) {
        fprintf(stderr, "Could not write to %s: %s. Aborting.\n", path,
                strerror(errno));
        close(file);
        free(path);
        return FALSE;
    }

    close(file);
    free(path);
    return TRUE;
}

static inline gboolean set_settle_device_conf(const char *uuid)
{
    char *content = malloc(strlen(device_conf_template) + 50);
    int i, j;
    gboolean ret;
    char uuid_escaped[50];

    if (content == NULL) {
        fprintf(stderr, "Could not malloc memory for content. Aborting.\n");
        return FALSE;
    }

    for (i = 0, j = 0; i < strlen(uuid) && j < sizeof(uuid_escaped); i++, j++) {
        if (uuid[i] == '-') {
            uuid_escaped[j++] = '\\';
            uuid_escaped[j++] = 'x';
            uuid_escaped[j++] = '2';
            uuid_escaped[j] = 'd';
        } else
            uuid_escaped[j] = uuid[i];
    }
    uuid_escaped[j < sizeof(uuid_escaped) ? j : sizeof(uuid_escaped)-1] = '\0';

    g_assert(sprintf(content, device_conf_template, uuid_escaped) > 0);

    ret = set_unit_conf(
            "home-mount-settle.service", device_conf_name, content);

    free(content);
    return ret;
}

static inline gboolean set_home_conf(const char *unit)
{
    return set_unit_conf(unit, home_conf_name, home_conf_content);
}

static inline gboolean write_systemd_configuration(invocation_data *data)
{
    printf("Writing systemd configuration.\n");
    if (!set_home_conf("multi-user.target") ||
            !set_home_conf("systemd-user-sessions.service") ||
            !set_settle_device_conf(data->cleartext_device_uuid) ||
            !set_unit_conf(
                "home.mount", settle_conf_name, settle_conf_content))
        return FALSE;
    return TRUE;
}

static inline void finish_if_ready(invocation_data *data)
{
    if (status == ENCRYPTION_RESCAN_FINISHED &&
            data->cleartext_device_uuid != NULL) {
        if (!write_systemd_configuration(data)) {
            end_encryption_to_failure(data);
            return;
        }

        set_status(ENCRYPTION_FINISHED);
        printf("Finished encryption successfully.\n");
        g_signal_handler_disconnect(data->object_manager, data->signal_handler);
        invocation_data_free(data);
    }
}

static void on_properties_changed(
        GDBusObjectManagerClient *manager,
        GDBusObjectProxy *object_proxy,
        GDBusProxy *interface_proxy,
        GVariant *changed_properties,
        const gchar* const *invalidated_properties,
        gpointer user_data)
{
    static gchar *cleartext_device_path = NULL;
    invocation_data *data = user_data;
    const gchar *interface, *object_path, *tmp;
    GVariantIter iter;
    gchar *key;
    GVariant *value;

    interface = g_dbus_proxy_get_interface_name(interface_proxy);
    object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object_proxy));

    if (strcmp(interface, "org.freedesktop.UDisks2.Encrypted") == 0) {
        if (strcmp(object_path, data->crypto_device_path) == 0) {
            g_variant_iter_init(&iter, changed_properties);
            while (g_variant_iter_loop(&iter, "{sv}", &key, &value)) {
                if (strcmp(key, "CleartextDevice") == 0) {
                    tmp = g_variant_get_string(value, NULL);
                    if (tmp != NULL && strcmp(tmp, "") != 0)
                        cleartext_device_path = g_strdup(tmp);
                    break;
                }
            }
        }
    } else if (strcmp(interface, "org.freedesktop.UDisks2.Block") == 0) {
        if (cleartext_device_path != NULL &&
                strcmp(object_path, cleartext_device_path) == 0) {
            g_variant_iter_init(&iter, changed_properties);
            while (g_variant_iter_loop(&iter, "{sv}", &key, &value)) {
                if (strcmp(key, "IdUUID") == 0) {
                    tmp = g_variant_get_string(value, NULL);
                    if (tmp != NULL && strcmp(tmp, "") != 0) {
                        g_free(cleartext_device_path);
                        cleartext_device_path = NULL;
                        data->cleartext_device_uuid = g_strdup(tmp);
                        finish_if_ready(data);
                    }
                    break;
                }
            }
        }
    }
}

static void rescan_complete(
        GObject *block,
        GAsyncResult *res,
        gpointer user_data)
{
    invocation_data *data = user_data;

    udisks_block_call_rescan_finish((UDisksBlock *)block, res, NULL);
    if (status == ENCRYPTION_NEEDS_RESCAN) {
        set_status(ENCRYPTION_RESCAN_FINISHED);
        finish_if_ready(data);
    }
}

static inline void create_empty_file(const char *path)
{
    int file = open(path, O_TRUNC | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    if (file != -1)
        close(file);
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

        if (data->passphrase_is_temporary)
            create_empty_file(TEMPORARY_KEY_FILE);
        else
            create_empty_file(UPDATE_KEY_FILE);

    } else {
        fprintf(stderr, "%s. Aborting.\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
        data = NULL;  // Data was freed in end_encryption_to_failure
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
    if (data->erase == ERASE_WITH_ZEROS)
        g_variant_builder_add(
                &builder, "{sv}", "erase", g_variant_new_string("zero"));

    g_variant_builder_init(&subbuilder, G_VARIANT_TYPE("a(sa{sv})"));
    g_variant_builder_open(&subbuilder, G_VARIANT_TYPE("(sa{sv})"));
    g_variant_builder_add(&subbuilder, "s", "crypttab");
    g_variant_builder_open(&subbuilder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
            &subbuilder, "{sv}", "passphrase-contents",
            g_variant_new_bytestring(""));
    g_variant_builder_add(
            &subbuilder, "{sv}", "options",
            g_variant_new_bytestring(
                "nofail,tries=0,timeout=0,x-systemd.device-timeout=0"));
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
                "defaults,noauto,noatime,x-systemd.device-timeout=0"));
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

static gboolean erase_data(gpointer user_data)
{
    static EVP_CIPHER_CTX *ctx;
    static long long count = 0;
    invocation_data *data = user_data;
    static unsigned char inbuf[BLOCK_SIZE];
    static gboolean initialized = FALSE;
    static unsigned char key[KEY_SIZE+1], iv[] = "1234567890123456";
    static unsigned char outbuf[BLOCK_SIZE];
    size_t len;
    int outlen = 0, i;
    static FILE *stream;

    if (!initialized) {
        /*
         * Use AES cipher with random key and zero data as random data
         * source. This should faster than reading /dev/urandom
         * continuosly and perfectly fine from security point of view.
         *
         * New ciphers have been introduced and AES could be replaced
         * with chacha that should be faster on devices without AES
         * instructions.
         */
        stream = fopen("/dev/urandom", "rb");
        if (fread(key, 1, KEY_SIZE, stream) < KEY_SIZE) {
            fprintf(stderr, "Warning: %s\n",
                    "Could not get random key. Skipping device erasure!");
            return FALSE;
        }
        key[KEY_SIZE] = '\0';
        fclose(stream);

        ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);

        for (i = 0; i < sizeof(inbuf); i++)
            inbuf[i] = 0;

        stream = fopen(STR(DEVICE_TO_ENCRYPT), "wb");

        initialized = TRUE;
    }

    if (!EVP_CipherUpdate(ctx, outbuf, &outlen, inbuf, sizeof(inbuf))) {
        fprintf(stderr, "Warning: %s\n",
                "Error with cipher. Device erasure incomplete.");

    } else if ((len = fwrite(outbuf, 1, outlen, stream)) < outlen) {
        if (len > 0)
            count += len;

        if (errno == ENOSPC) {  // Finished successfully
            // Left out EVP_EncryptFinal_ex since those
            // last few bytes are not needed here
            printf("Wrote %lld bytes to erased block device.\n", count);

        } else {
            fprintf(stderr,
                    "Warning: Writing failed after %lld bytes: %s. %s\n",
                    count, strerror(errno), "Device erasure incomplete.");
        }
    } else {  // Wrote the whole block, not done yet
        count += len;
        return TRUE;  // Not finished, continue looping
    }

    // Got here, erasure finished or incomplete. Continue to next task.
    start_format_luks(data);
    EVP_CIPHER_CTX_free(ctx);
    return FALSE;  // End looping
}

static inline void tear_down_complete(
        GObject *block,
        GAsyncResult *res,
        gpointer user_data)
{
    invocation_data *data = user_data;
    GError *error = NULL;

    if (udisks_block_call_format_finish((UDisksBlock *)block, res, &error)) {
        printf("Removed /home from fstab. Starting to erase.\n");
        g_idle_add(erase_data, data);

    } else {
        fprintf(stderr, "%s. Aborting.\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
        data = NULL;  // Data was freed in end_encryption_to_failure
    }
}

static void start_erasure_with_random(invocation_data *data)
{
    GVariantBuilder builder;
    GVariant *options;

    set_status(ENCRYPTION_ERASURE_IN_PROGRESS);

    // Tear down all configuration and wipe file system signature
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
            &builder, "{sv}", "no-block", g_variant_new_boolean(TRUE));
    g_variant_builder_add(
            &builder, "{sv}", "dry-run-first", g_variant_new_boolean(TRUE));
    g_variant_builder_add(
            &builder, "{sv}", "tear-down", g_variant_new_boolean(TRUE));
    options = g_variant_builder_end(&builder);

    udisks_block_call_format(
            data->block, "empty", options, NULL, tear_down_complete, data);
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
    const gchar *const *mount_points;
    UDisksFilesystem *udfs;

    if (!udisks_manager_call_can_format_finish(
                (UDisksManager *)manager, &avail, res, &error)) {
        fprintf(stderr, "%s. Aborting.\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
        return;
    }

    g_variant_get(avail, "(bs)", &available, &bin);

    if (!available) {
        fprintf(stderr, "%s is not available, needs %s. Aborting.\n",
                STR(FILESYSTEM_FORMAT), bin);
        end_encryption_to_failure(data);
        return;
    }

    // Unmount if mounted, otherwise encryption fails
    udfs = udisks_filesystem_proxy_new_sync(
            data->connection, G_DBUS_PROXY_FLAGS_NONE, UDISKS_INTERFACE,
            data->crypto_device_path, NULL, &error);
    if (!udfs) {
        fprintf(stderr, "%s. Aborting.\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
    } else {
        mount_points = udisks_filesystem_get_mount_points(udfs);
        // If the array is not empty, there is a mount point
        if (mount_points != NULL && *mount_points != NULL) {
            if (!udisks_filesystem_call_unmount_sync(
                        udfs, g_variant_new("a{sv}", NULL), NULL, &error)) {
                fprintf(stderr, "%s. Aborting.\n", error->message);
                end_encryption_to_failure(data);
                g_error_free(error);
                return;
            } else {
                printf("Unmounted %s\n", data->crypto_device_path);
            }
        }
    }

    printf("Starting encryption. All data will be destroyed.\n");
    if (data->erase == ERASE_WITH_RANDOM) {
        start_erasure_with_random(data);
    } else {
        start_format_luks(data);
    }
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
        fprintf(stderr, "%s. Aborting.\n", error->message);
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
    invocation_data *data = user_data;
    gchar **devices;
    GVariant *devnames, *dev;
    GError *error = NULL;
    GVariantIter *iter;
    gsize length;

    if (!udisks_manager_call_resolve_device_finish(
                (UDisksManager *)manager, &devices, res, &error)) {
        fprintf(stderr, "%s. Aborting.\n", error->message);
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
    data->crypto_device_path = g_variant_dup_string(dev, &length);

    data->signal_handler = g_signal_connect(
            data->object_manager, "interface-proxy-properties-changed",
            G_CALLBACK(on_properties_changed), data);

    udisks_block_proxy_new(
            data->connection, G_DBUS_PROXY_FLAGS_NONE, UDISKS_INTERFACE,
            data->crypto_device_path, NULL, found_block_device, data);

    g_variant_unref(dev);
    g_variant_iter_free(iter);
    g_free(devices);
    g_variant_unref(devnames);
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

static void got_client(GObject *proxy, GAsyncResult *res, gpointer user_data)
{
    invocation_data *data = user_data;
    GError *error = NULL;

    data->client = udisks_client_new_finish(res, &error);
    if (data->client == NULL) {
        fprintf(stderr, "%s\n", error->message);
        end_encryption_to_failure(data);
        g_error_free(error);
        return;
    }

    data->manager = udisks_client_get_manager(data->client);
    data->object_manager = udisks_client_get_object_manager(data->client);

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

    udisks_client_new(NULL, got_client, data);
}

gboolean start_to_encrypt(
        gchar *passphrase,
        gboolean passphrase_is_temporary,
        erase_t erase)
{
    invocation_data *data;

    if (status != ENCRYPTION_NOT_STARTED)
        return FALSE;

    set_status(ENCRYPTION_IN_PREPARATION);
    data = g_new0(invocation_data, 1);
    data->passphrase = passphrase;
    data->passphrase_is_temporary = passphrase_is_temporary;
    data->erase = erase;
    g_bus_get(G_BUS_TYPE_SYSTEM, NULL, got_bus, data);
    return TRUE;
}

encryption_state get_encryption_status(void)
{
    return status;
}

// vim: expandtab:ts=4:sw=4
