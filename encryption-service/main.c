/* Copyright (c) 2019 Jolla Ltd. */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include "dbus.h"
#include "encrypt.h"
#include "manage.h"

#define TEMPORARY_PASSPHRASE "00000"
#define QUIT_TIMEOUT 60

G_DEFINE_QUARK(encryption-error-quark, encryption_error)
#define ENCRYPTION_ERROR (encryption_error_quark())

typedef enum {
  ENCRYPTION_ERROR_FAILED,
  ENCRYPTION_ERROR_BUSY
} EncryptionError;

static GMainLoop *main_loop;
static gchar *saved_passphrase = NULL;
static erase_t erase_type = DONT_ERASE;

static gboolean call_prepare(gchar *passphrase, erase_t erase, GError **error)
{
    if (get_encryption_status() == ENCRYPTION_NOT_STARTED) {
        saved_passphrase = passphrase;
        erase_type = erase;

        prepare(main_loop);

        return TRUE;
    } else {
        g_free(passphrase);
        g_set_error_literal(
                error, ENCRYPTION_ERROR, ENCRYPTION_ERROR_FAILED,
                "Encryption is already work in progress");
        return FALSE;
    }
}

static gboolean call_encrypt(
        GError **error)
{
    gchar *passphrase = saved_passphrase;
    gboolean passphrase_is_temporary = FALSE;
    if (saved_passphrase == NULL) {
        passphrase_is_temporary = TRUE;
        passphrase = g_strdup(TEMPORARY_PASSPHRASE);
    }
    if (!start_to_encrypt(passphrase, passphrase_is_temporary, erase_type)) {
        g_set_error_literal(
                error, ENCRYPTION_ERROR, ENCRYPTION_ERROR_FAILED,
                "Starting encryption failed");
        return FALSE;
    }

    return TRUE;
}

static gboolean call_finalize(GError **error)
{
    switch (get_encryption_status()) {
        case ENCRYPTION_NOT_STARTED:
        case ENCRYPTION_FINISHED:
            finalize(main_loop);
            return TRUE;
        default:
            g_set_error_literal(
                    error, ENCRYPTION_ERROR, ENCRYPTION_ERROR_BUSY,
                    "Encryption is in progress");
            return FALSE;
    }
}

static gboolean quit_if_idle(gpointer user_data) {
    if (get_encryption_status() == ENCRYPTION_NOT_STARTED &&
            saved_passphrase == NULL)
        g_main_loop_quit(main_loop);
    return FALSE;
}

static void status_changed_handler(encryption_state status)
{
    GError *error = NULL;
    switch (status) {
        case ENCRYPTION_FAILED:
            g_set_error_literal(
                    &error, ENCRYPTION_ERROR, ENCRYPTION_ERROR_FAILED,
                    "Encryption failed");
            signal_encrypt_finished(error);
            g_main_loop_quit(main_loop);
            g_error_free(error);
            break;
        case ENCRYPTION_FINISHED:
            signal_encrypt_finished(NULL);
            break;
        default:
            break; // Do nothing
    }
}

int main(int argc, char **argv)
{
    setlinebuf(stdout);
    main_loop = g_main_loop_new(NULL, FALSE);

    init_encryption_service(status_changed_handler);
    init_dbus(call_prepare, call_encrypt, call_finalize);
    g_timeout_add_seconds(QUIT_TIMEOUT, quit_if_idle, NULL);
    g_main_loop_run(main_loop);

    switch (get_encryption_status()) {
        case ENCRYPTION_NOT_STARTED:
        case ENCRYPTION_FINISHED:
            return EXIT_SUCCESS;
        default:
            return EXIT_FAILURE;
    }
}

// vim: expandtab:ts=4:sw=4
