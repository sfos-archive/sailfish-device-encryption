/* Copyright (c) 2019 Jolla Ltd. */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include "dbus.h"
#include "encrypt.h"

#define ENCRYPTION_ERROR_FAILED encryption_error_failed()

GQuark encryption_error_failed(void)
{
    return g_quark_from_static_string("g-encryption-error-quark");
}

GMainLoop *main_loop;

static gboolean call_encrypt(gchar *passphrase, GError **error)
{
    if (!start_to_encrypt(passphrase)) {
        g_set_error_literal(
                error, ENCRYPTION_ERROR_FAILED, 0,
                "Starting encryption failed");
        g_main_loop_quit(main_loop);
        return FALSE;
    }

    return TRUE;
}

static void status_changed_handler(encryption_state status)
{
    GError *error = NULL;
    switch (status) {
        case ENCRYPTION_FAILED:
            g_set_error_literal(
                    &error, ENCRYPTION_ERROR_FAILED, 0,
                    "Encryption failed");
            // Fall through
        case ENCRYPTION_FINISHED:
            signal_encrypt_finished(error);
            g_main_loop_quit(main_loop);
            break;
        default:
            break; // Do nothing
    }
}

int main(int argc, char **argv)
{
    main_loop = g_main_loop_new(NULL, FALSE);

    init_encryption_service(status_changed_handler);
    init_dbus(call_encrypt);
    g_main_loop_run(main_loop);

    return (get_encryption_status() == ENCRYPTION_FINISHED) ?
            EXIT_SUCCESS : EXIT_FAILURE;
}

// vim: expandtab:ts=4:sw=4
