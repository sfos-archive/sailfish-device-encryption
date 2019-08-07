/*
 * DBus service interfaces.
 *
 * Copyright (c) 2019 Jolla Ltd.
 */

#include <dbusaccess_peer.h>
#include <dbusaccess_policy.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include "dbus.h"

#define ACCESS_DENIED_ERROR "org.freedesktop.DBus.Error.AccessDenied"
#define BUS_NAME "org.sailfishos.EncryptionService"
#define ENCRYPTION_IFACE BUS_NAME
#define ENCRYPTION_PATH "/org/sailfishos/EncryptionService"
#define ENCRYPTION_FAILED_ERROR BUS_NAME ".Failed"
#define PREPARE_TO_ENCRYPT_METHOD "PrepareToEncrypt"
#define ENCRYPTION_METHOD "BeginEncryption"
#define ENCRYPTION_FINISHED_SIGNAL "EncryptionFinished"
#define FINALIZATION_METHOD "FinalizeEncryption"
#define PRIVILEGED_ONLY_POLICY "1;user(nemo:privileged) = allow;"

static const gchar introspection_xml[] =
    "<node>"
    "<interface name=\"" ENCRYPTION_IFACE "\">"
    "<method name=\"" ENCRYPTION_METHOD "\">"
    "</method>"
    "<method name=\"" PREPARE_TO_ENCRYPT_METHOD "\">"
    "<arg name=\"passphrase\" direction=\"in\" type=\"s\"></arg>"
    "<arg name=\"overwriteType\" direction=\"in\" type=\"s\"></arg>"
    "</method>"
    "<method name=\"" FINALIZATION_METHOD "\">"
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
    prepare_call_handler prepare_method;
    encrypt_call_handler encrypt_method;
    finalize_call_handler finalize_method;
    DAPolicy *policy;
    gchar *receiver;
} data;

static gboolean is_allowed(GDBusConnection *connection, const gchar *sender);

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
    gchar *passphrase, *overwrite_type;
    erase_t erase;

    if (!is_allowed(connection, sender)) {
        g_dbus_method_invocation_return_dbus_error(
                invocation, ACCESS_DENIED_ERROR, "Access denied");
        return;
    }

    // Currently doesn't check path or interface name because
    // this implements only one interface on only one path
    // and GDBus checks for them and also that parameters exist
    // and are of correct type.
    if (strcmp(method_name, PREPARE_TO_ENCRYPT_METHOD) == 0) {
        g_variant_iter_init(&iter, parameters);
        g_variant_iter_next(&iter, "s", &passphrase);
        g_variant_iter_next(&iter, "s", &overwrite_type);

        if (strcmp(overwrite_type, "none") == 0) {
            erase = DONT_ERASE;
        } else if (strcmp(overwrite_type, "zero") == 0) {
            erase = ERASE_WITH_ZEROS;
        } else if (strcmp(overwrite_type, "random") == 0) {
            erase = ERASE_WITH_RANDOM;
        } else {
            g_dbus_method_invocation_return_dbus_error(
                    invocation, ENCRYPTION_FAILED_ERROR,
                    "Invalid argument to overwriteType");
            g_free(passphrase);
            g_free(overwrite_type);
            return;
        }
        g_free(overwrite_type);

        // Prepare method takes the ownership of passphrase now
        if (data.prepare_method(passphrase, erase, &error)) {
            g_dbus_method_invocation_return_value(invocation, NULL);
        } else {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
        }
    } else if (strcmp(method_name, ENCRYPTION_METHOD) == 0) {
        if (data.encrypt_method(&error)) {
            g_dbus_method_invocation_return_value(invocation, NULL);
            data.receiver = g_strdup(sender);
        } else {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
        }
    } else if (strcmp(method_name, FINALIZATION_METHOD) == 0) {
        if (data.finalize_method(&error)) {
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

void init_dbus(
        prepare_call_handler prepare_method,
        encrypt_call_handler encrypt_method,
        finalize_call_handler finalize_method)
{
    data.prepare_method = prepare_method;
    data.encrypt_method = encrypt_method;
    data.finalize_method = finalize_method;

    data.policy = da_policy_new(PRIVILEGED_ONLY_POLICY);
    data.receiver = NULL;

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
            data.connection, data.receiver,
            ENCRYPTION_PATH, ENCRYPTION_IFACE, ENCRYPTION_FINISHED_SIGNAL,
            tuple, &dbus_error)) {
        fprintf(stderr, "%s\n", dbus_error->message);
        g_error_free(dbus_error);
    }

    g_free(data.receiver);
    data.receiver = NULL;
}

static gboolean is_allowed(GDBusConnection *connection, const gchar *sender)
{
    DAPeer *peer;

    peer = da_peer_get(DA_BUS_SYSTEM, sender);
    if (peer == NULL)
        return FALSE;

    if (da_policy_check(
            data.policy, &peer->cred, 0, NULL,
            DA_ACCESS_DENY) == DA_ACCESS_DENY) {
        return FALSE;
    }

    return TRUE;
}

// vim: expandtab:ts=4:sw=4
