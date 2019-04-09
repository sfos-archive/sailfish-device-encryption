/* Copyright (c) 2019 Jolla Ltd. */

#ifndef __DBUS_H
#define __DBUS_H

typedef gboolean (*encrypt_call_handler)(gchar *passphrase, GError **error);

void init_dbus(encrypt_call_handler);
void signal_encrypt_finished(GError *error);

#endif // __DBUS_H