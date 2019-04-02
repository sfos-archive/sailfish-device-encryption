/*
 * DBus service interfaces.
 *
 * Copyright (c) 2019 Jolla Ltd.
 */

#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include "dbus.h"

#define BUS_NAME "org.sailfishos.EncryptionService"
#define ENCRYPTION_IFACE BUS_NAME
#define ENCRYPTION_PATH "/org/sailfishos/EncryptionService"
#define ENCRYPTION_FAILED_ERROR BUS_NAME ".Failed"
#define ENCRYPTION_METHOD "BeginEncryption"
#define ENCRYPTION_FINISHED_SIGNAL "EncryptionFinished"

static const gchar introspection_xml[] =
    "<node>"
    "<interface name=\"" ENCRYPTION_IFACE "\">"
    "<method name=\"" ENCRYPTION_METHOD "\">"
    "<arg name=\"passphrase\" direction=\"in\" type=\"s\"></arg>"
    "</method>"
    "<signal name=\"" ENCRYPTION_FINISHED_SIGNAL "\">"
    "<arg name=\"success\" type=\"b\" />"
    "<arg name=\"error\" type=\"s\" />"
    "</signal>"
    "</interface>"
    "</node>";

static struct {
    GDBusConnection *connection;
    guint name_id;
    GDBusNodeInfo *info;
    guint encrypt_iface_id;
    GDBusInterfaceVTable *encrypt_iface_vtable;
    encrypt_call_handler encrypt_method;
} data;

static void bus_acquired_handler(
        GDBusConnection *connection,
        const gchar *name,
        gpointer user_data)
{
    GError *error = NULL;

    printf("Acquired bus for %s\n", name);

    data.connection = connection;
    data.encrypt_iface_id = g_dbus_connection_register_object(
            connection, ENCRYPTION_PATH,
            g_dbus_node_info_lookup_interface(data.info, ENCRYPTION_IFACE),
            data.encrypt_iface_vtable,
            NULL, NULL, &error);

    if (data.encrypt_iface_id == 0) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        return;
    }
}

static void name_acquired_handler(
        GDBusConnection *connection,
        const gchar *name,
        gpointer user_data)
{
    printf("Acquired dbus name %s\n", name);
}

static void name_lost_handler(
        GDBusConnection *connection,
        const gchar *name,
        gpointer user_data)
{
    printf("Lost dbus name %s\n", name);

    if (connection == NULL)
        data.connection = connection;
}

void method_call_handler(
        GDBusConnection *connection,
        const gchar *sender,
        const gchar *object_path,
        const gchar *interface_name,
        const gchar *method_name,
        GVariant *parameters,
        GDBusMethodInvocation *invocation,
        gpointer user_data)
{
    GError *error = NULL;
    GVariantIter iter;
    gchar *passphrase;

    // Currently doesn't check path or interface name because
    // this implements only one method and GDBus checks for other things
    if (strcmp(method_name, ENCRYPTION_METHOD) == 0) {
        g_variant_iter_init(&iter, parameters);
        g_variant_iter_next(&iter, "s", &passphrase);

        if (data.encrypt_method(passphrase, &error)) {
            g_dbus_method_invocation_return_value(invocation, NULL);
        } else {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
        }
    } else {
        g_dbus_method_invocation_return_dbus_error(
                invocation, ENCRYPTION_FAILED_ERROR,
                "Unknown method");  // This should never happen
    }
}

void init_dbus(encrypt_call_handler encrypt_method)
{
    data.encrypt_method = encrypt_method;

    data.info = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_assert(data.info != NULL);

    data.encrypt_iface_vtable = g_new0(GDBusInterfaceVTable, 1);
    data.encrypt_iface_vtable->method_call = method_call_handler;

    data.name_id = g_bus_own_name(
            G_BUS_TYPE_SYSTEM, BUS_NAME,
            G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
            bus_acquired_handler, name_acquired_handler,
            name_lost_handler, NULL, NULL);
}

void signal_encrypt_finished(GError *error)
{
    GError *dbus_error = NULL;
    GVariant **parameters, *tuple;

    parameters = g_new(GVariant *, 2);
    parameters[0] = g_variant_new_boolean(error == NULL);

    if (error != NULL)
        parameters[1] = g_variant_new_string(error->message);
    else
        parameters[1] = g_variant_new_string("");

    tuple = g_variant_new_tuple(parameters, 2);

    if (!g_dbus_connection_emit_signal(
            data.connection, NULL,  // TODO: Could be a subscribed listener
            ENCRYPTION_PATH, ENCRYPTION_IFACE, ENCRYPTION_FINISHED_SIGNAL,
            tuple, &dbus_error)) {
        fprintf(stderr, "%s\n", dbus_error->message);
        g_error_free(dbus_error);
    }

    if (error != NULL)
        g_error_free(error);
}

// vim: expandtab:ts=4:sw=4
