/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#ifndef __DBUS_H
#define __DBUS_H

#include "erase.h"

typedef gboolean (*encrypt_call_handler)(GError **error);
typedef gboolean (*prepare_call_handler)(
        gchar *passphrase,
        erase_t erase,
        GError **error);
typedef gboolean (*finalize_call_handler)(GError **error);

void init_dbus(
        prepare_call_handler prepare_method,
        encrypt_call_handler encrypt_method,
        finalize_call_handler finalize_method);
void signal_encrypt_finished(GError *error);

#endif // __DBUS_H
