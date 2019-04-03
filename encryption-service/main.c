/* Copyright (c) 2019 Jolla Ltd. */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include "dbus.h"
#include "encrypt.h"
#include "manage.h"

#define ENCRYPTION_ERROR_FAILED encryption_error_failed()
#define ENCRYPTION_ERROR_BUSY encryption_error_busy()

GQuark encryption_error_failed(void)
{
    return g_quark_from_static_string("g-encryption-error-failed-quark");
}

GQuark encryption_error_busy(void)
{
    return g_quark_from_static_string("g-encryption-error-busy-quark");
}

GMainLoop *main_loop;

static gboolean call_encrypt(gchar *passphrase, GError **error)
{
    if (!start_to_encrypt(passphrase)) {
        g_set_error_literal(
                error, ENCRYPTION_ERROR_FAILED, 0,
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
            g_idle_add(finalize, main_loop);
            return TRUE;
        default:
            g_set_error_literal(
                    error, ENCRYPTION_ERROR_BUSY, 0,
                    "Encryption is in progress");
            return FALSE;
    }
}

static void status_changed_handler(encryption_state status)
{
    GError *error = NULL;
    switch (status) {
        case ENCRYPTION_FAILED:
            g_set_error_literal(
                    &error, ENCRYPTION_ERROR_FAILED, 0,
                    "Encryption failed");
            signal_encrypt_finished(error);
            g_main_loop_quit(main_loop);
            break;
        case ENCRYPTION_FINISHED:
            signal_encrypt_finished(error);
            break;
        default:
            break; // Do nothing
    }
}

int main(int argc, char **argv)
{
    main_loop = g_main_loop_new(NULL, FALSE);

    init_encryption_service(status_changed_handler);
    init_dbus(call_encrypt, call_finalize);
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
