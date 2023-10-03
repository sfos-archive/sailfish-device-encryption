/****************************************************************************************
** Copyright (c) 2021 - 2023 Jolla Ltd.
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

#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <dbusaccess_policy.h>
#include <dbusaccess_peer.h>
#include "homecopy.h"

#define QUIT_TIMEOUT 60
#define ACCESS_DENIED_ERROR "org.freedesktop.DBus.Error.AccessDenied"
#define BUS_NAME "org.sailfishos.HomeCopyService"
#define COPY_FAILED_ERROR BUS_NAME ".Failed"
#define COPY_FAIL_CODE 1
#define SD_COPY_IFACE BUS_NAME
#define SD_COPY_PATH "/org/sailfishos/HomeCopyService"
#define COPY_HOME_METHOD "copyHome"
#define RESTORE_HOME_METHOD "restoreHome"
#define SET_COPY_LOCATION_METHOD "setCopyDevice"
#define COPY_DONE_SIGNAL "copyDone"
#define PRIVILEGED_ONLY_POLICY "1;group(privileged) = allow;"

G_DEFINE_QUARK(copy-error-quark, copy_error)
#define COPY_ERROR (copy_error_quark())

static gboolean is_allowed(GDBusConnection *connection, const gchar *sender);

static const gchar introspection_xml[] =
    "<node>"
    "<interface name=\"" SD_COPY_IFACE "\">"
    "<method name=\"" COPY_HOME_METHOD "\">"
    "</method>"
    "<method name=\"" RESTORE_HOME_METHOD "\">"
    "</method>"
    "<method name=\"" SET_COPY_LOCATION_METHOD "\">"
    "<arg name=\"devpath\" direction=\"in\" type=\"s\"></arg>"
    "</method>"
    "<signal name=\"" COPY_DONE_SIGNAL "\">"
    "<arg name=\"success\" type=\"b\" />"
    "</signal>"
    "</interface>"
    "</node>";

static struct {
    DAPolicy *policy;
    GDBusConnection *connection;
    GDBusNodeInfo *introspection_data;
    GDBusInterfaceVTable *iface_vtable;
    GMainLoop *main_loop;
} data;

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data)
{
    GError *error = NULL;
    guint iface_id;
    data.connection = connection;
    iface_id = g_dbus_connection_register_object(
            data.connection, SD_COPY_PATH,
            g_dbus_node_info_lookup_interface(data.introspection_data, SD_COPY_IFACE),
            data.iface_vtable,
            NULL, NULL, &error);

    if (iface_id == 0) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        return;
  }
}

static void on_name_acquired(GDBusConnection *connection,
                             const gchar     *name,
                             gpointer         user_data)
{
    printf("Acquired dbus name %s\n", name);
}

static void on_name_lost(GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
    fprintf(stderr, "Failed to own name %s \n", name);
    exit(EXIT_FAILURE);
}

static gboolean quit_if_idle(gpointer user_data)
{
    if (get_copy_state() !=  COPYING) {
        g_main_loop_quit(data.main_loop);
        return FALSE;
    }
    return TRUE;
}

void signal_copy_result(copy_result res)
{
    GError *error = NULL;
    if (res == COPY_PROCESS_ERROR) {
        fprintf(stderr, "Failed to start copying\n");
    }
    if (!g_dbus_connection_emit_signal(data.connection,
                                       NULL, SD_COPY_PATH,
                                       SD_COPY_IFACE, COPY_DONE_SIGNAL,
                                       g_variant_new("(b)", res == COPY_SUCCESS),
                                       &error)) {
        fprintf(stderr, "Failed to emit signal: %s \n", error->message);
    }
}

static void handle_method_call(GDBusConnection       *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data)
{
    GVariantIter iter;
    gchar *copy_path;
    GError *error = NULL;

    if (!is_allowed(connection, sender)) {
        g_dbus_method_invocation_return_dbus_error(
                invocation, ACCESS_DENIED_ERROR, "Access denied");
        return;
    }

    if (get_copy_state() == COPYING) {
        g_set_error_literal(&error, COPY_ERROR, COPY_FAIL_CODE,
                            "Copy operation already running");
        g_dbus_method_invocation_return_gerror(invocation, error);
        g_error_free(error);
        return;
    }

    if (strcmp(method_name, COPY_HOME_METHOD) == 0) {
        copy_home(data.main_loop, signal_copy_result);
    } else if (strcmp(method_name, RESTORE_HOME_METHOD) == 0) {
        restore_home(data.main_loop, signal_copy_result);
    } else if (strcmp(method_name, SET_COPY_LOCATION_METHOD) == 0) {
        g_variant_iter_init(&iter, parameters);
        g_variant_iter_next(&iter, "s", &copy_path);
        if (!set_copy_location(copy_path)) {
            g_set_error_literal(
                &error, COPY_ERROR, COPY_FAIL_CODE,
                "Setting copy location failed");
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
        }
        g_free(copy_path);
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
                                                   COPY_FAILED_ERROR,
                                                   "unknown method");
    }
}

static gboolean is_allowed(GDBusConnection *connection, const gchar *sender)
{
    DAPeer *peer;
    data.policy = da_policy_new(PRIVILEGED_ONLY_POLICY);

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

int main(int argc, char **argv)
{
    setlinebuf(stdout);
    data.main_loop = g_main_loop_new(NULL, FALSE);
    data.introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    data.policy = da_policy_new(PRIVILEGED_ONLY_POLICY);
    g_assert(data.introspection_data != NULL);

    data.iface_vtable = g_new0(GDBusInterfaceVTable, 1);
    data.iface_vtable->method_call = handle_method_call;

    guint owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
                                    BUS_NAME,
                                    G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                    on_bus_acquired,
                                    on_name_acquired,
                                    on_name_lost,
                                    NULL,
                                    NULL);

    g_timeout_add_seconds(QUIT_TIMEOUT, quit_if_idle, NULL);
    g_main_loop_run(data.main_loop);
    g_bus_unown_name(owner_id);
    g_dbus_node_info_unref(data.introspection_data);
    g_free(data.iface_vtable);
    return EXIT_SUCCESS;
}

