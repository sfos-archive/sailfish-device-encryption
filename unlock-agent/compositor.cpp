/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include "compositor.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#include <sailfish-minui/eventloop.h>

namespace Sailfish {

/* ========================================================================= *
 * Constants
 * ========================================================================= */

#define COMPOSITOR_SERVICE                       "org.nemomobile.compositor"
#define COMPOSITOR_PATH                          "/"
#define COMPOSITOR_IFACE                         "org.nemomobile.compositor"
#define COMPOSITOR_SET_UPDATES_ENABLED           "setUpdatesEnabled"
#define COMPOSITOR_GET_TOPMOST_WINDOW_PID        "privateTopmostWindowProcessId"
#define COMPOSITOR_TOPMOST_WINDOW_PID_CHANGED    "privateTopmostWindowProcessIdChanged"

#define SYSTEMD_SERVICE                          "org.freedesktop.systemd1"
#define SYSTEMD_MANAGER_PATH                     "/org/freedesktop/systemd1"
#define SYSTEMD_TARGET_UNIT_PATH                 "/org/freedesktop/systemd1/unit/home_2emount"

#define SYSTEMD_MANAGER_INTERFACE                "org.freedesktop.systemd1.Manager"
#define SYSTEMD_MANAGER_METHOD_LIST_UNITS        "ListUnits"
#define SYSTEMD_MANAGER_METHOD_SUBSCRIBE         "Subscribe"
#define SYSTEMD_MANAGER_METHOD_UNSUBSCRIBE       "Unsubscribe"

#define DBUS_PROPERTIES_INTERFACE                "org.freedesktop.DBus.Properties"
#define DBUS_PROPERTIES_METHOD_GET               "Get"
#define DBUS_PROPERTIES_METHOD_GET_ALL           "GetAll"
#define DBUS_PROPERTIES_METHOD_SET               "Set"
#define DBUS_PROPERTIES_SIGNAL_CHANGED           "PropertiesChanged"

#define SYSTEMD_UNIT_INTERFACE                   "org.freedesktop.systemd1.Unit"
#define SYSTEMD_MOUNT_PROPERTY_ACTIVE_STATE      "ActiveState"
#define SYSTEMD_MOUNT_PROPERTY_SUB_STATE         "SubState"

/* While testing/debugging, it can be easier to start/stop
 * for example mce service rather than mount/unmount /home ...
 */
#if 0
#undef SYSTEMD_TARGET_UNIT_PATH
#define SYSTEMD_TARGET_UNIT_PATH "/org/freedesktop/systemd1/unit/mce_2eservice"
#endif

/* ========================================================================= *
 * Helpers
 * ========================================================================= */

#if LOGGING_ENABLE_DEBUG
static const char *stringRepr(const char *str)
{
    return !str ? "<null>" : !*str ? "<empty>" : str;
}
#endif

static bool equal(const char *pat, const char *str)
{
    // both are null or the same string
    return !pat ? !str : str ? !strcmp(pat, str) : false;
}

static bool equal_or_dontcare(const char *pat, const char *str)
{
    // pat is null or the same as str, but null str does not match anything
    return !str ? false : !pat ? true : !strcmp(pat, str);
}

/* ========================================================================= *
 * D-Bus glue
 * ========================================================================= */

static const char *dbushelper_typename(int type)
{
    const char *name = "UNKNOWN";
    switch (type) {
    case DBUS_TYPE_INVALID:     name = "INVALID";     break;
    case DBUS_TYPE_BYTE:        name = "BYTE";        break;
    case DBUS_TYPE_BOOLEAN:     name = "BOOLEAN";     break;
    case DBUS_TYPE_INT16:       name = "INT16";       break;
    case DBUS_TYPE_UINT16:      name = "UINT16";      break;
    case DBUS_TYPE_INT32:       name = "INT32";       break;
    case DBUS_TYPE_UINT32:      name = "UINT32";      break;
    case DBUS_TYPE_INT64:       name = "INT64";       break;
    case DBUS_TYPE_UINT64:      name = "UINT64";      break;
    case DBUS_TYPE_DOUBLE:      name = "DOUBLE";      break;
    case DBUS_TYPE_STRING:      name = "STRING";      break;
    case DBUS_TYPE_OBJECT_PATH: name = "OBJECT_PATH"; break;
    case DBUS_TYPE_SIGNATURE:   name = "SIGNATURE";   break;
    case DBUS_TYPE_UNIX_FD:     name = "UNIX_FD";     break;
    case DBUS_TYPE_ARRAY:       name = "ARRAY";       break;
    case DBUS_TYPE_VARIANT:     name = "VARIANT";     break;
    case DBUS_TYPE_STRUCT:      name = "STRUCT";      break;
    case DBUS_TYPE_DICT_ENTRY:  name = "DICT_ENTRY";  break;
    default: break;
    }
    return name;
}

static int
dbushelper_at_type(DBusMessageIter *iter)
{
    return dbus_message_iter_get_arg_type(iter);
}

static bool
dbushelper_at_end(DBusMessageIter *iter)
{
    return dbushelper_at_type(iter) == DBUS_TYPE_INVALID;
}

static bool
dbushelper_req_type(DBusMessageIter *iter, int type)
{
    int have = dbushelper_at_type(iter);
    if (have == type)
        return true;

    log_err("dbus message parse error: expected "
            << dbushelper_typename(type) << ", got "
            << dbushelper_typename(have));
    return false;
}

#ifdef DEAD_CODE
static bool
dbushelper_get_int32(DBusMessageIter *iter, int *val)
{
    if (!dbushelper_req_type(iter, DBUS_TYPE_INT32))
        return false;

    dbus_int32_t dta = 0;
    dbus_message_iter_get_basic(iter, &dta);
    dbus_message_iter_next(iter);
    return *val = (int)dta, true;
}
#endif

static bool
dbushelper_get_string(DBusMessageIter *iter, const char **val)
{
    if (!dbushelper_req_type(iter, DBUS_TYPE_STRING))
        return false;

    const char *dta = 0;
    dbus_message_iter_get_basic(iter, &dta);
    dbus_message_iter_next(iter);
    return *val = dta, true;
}

static bool
dbushelper_get_variant(DBusMessageIter *iter, DBusMessageIter *sub)
{
    if (!dbushelper_req_type(iter, DBUS_TYPE_VARIANT))
        return false;

    dbus_message_iter_recurse(iter, sub);
    dbus_message_iter_next(iter);
    return true;
}

static bool
dbushelper_get_array(DBusMessageIter *iter, DBusMessageIter *sub)
{
    if (!dbushelper_req_type(iter, DBUS_TYPE_ARRAY))
        return false;

    dbus_message_iter_recurse(iter, sub);
    dbus_message_iter_next(iter);
    return true;
}

static bool
dbushelper_get_dentry(DBusMessageIter *iter, DBusMessageIter *sub)
{
    if (!dbushelper_req_type(iter, DBUS_TYPE_DICT_ENTRY))
        return false;

    dbus_message_iter_recurse(iter, sub);
    dbus_message_iter_next(iter);
    return true;
}

#ifdef DEAD_CODE
static bool
dbushelper_get_struct(DBusMessageIter *iter, DBusMessageIter *sub)
{
    if (!dbushelper_req_type(iter, DBUS_TYPE_STRUCT))
        return false;

    dbus_message_iter_recurse(iter, sub);
    dbus_message_iter_next(iter);
    return true;
}
#endif

static bool
dbushelper_send_va(DBusConnection *connection,
             const char *service,
             const char *path,
             const char *interface,
             const char *member,
             DBusPendingCallNotifyFunction callback,
             void *user_data,
             DBusFreeFunction user_free,
             DBusPendingCall **ppc,
             int first_arg_type,
             va_list va)
{
    bool res = false;
    DBusMessage *msg = 0;
    if ((msg = dbus_message_new_method_call(service, path, interface, member))) {
        if (first_arg_type != DBUS_TYPE_INVALID &&
            !dbus_message_append_args_valist(msg, first_arg_type, va)) {
            log_err("failed to append arguments to D-Bus message: "
                    << interface << "." <<  member);
        } else if (!callback) {
            dbus_message_set_no_reply(msg, true);
            if (dbus_connection_send(connection, msg, 0))
                res = true;
        } else {
            const int timeout = -1;
            DBusPendingCall *pc = 0;
            if (dbus_connection_send_with_reply(connection, msg, &pc, timeout)) {
                if (pc) {
                    if (dbus_pending_call_set_notify(pc, callback, user_data, user_free)) {
                        user_data = 0;
                        user_free = 0;
                        if (ppc)
                            *ppc = dbus_pending_call_ref(pc);
                        res = true;
                    }
                    dbus_pending_call_unref(pc);
                }
            }
        }
        dbus_message_unref(msg);
    }
    if (user_free)
        user_free(user_data);
    return res;
}

static bool
dbushelper_send(DBusConnection *connection,
                const char *service,
                const char *path,
                const char *interface,
                const char *member,
                DBusPendingCallNotifyFunction callback,
                void       *user_data,
                DBusFreeFunction user_free,
                DBusPendingCall **ppc,
                int first_arg_type, ...)
{
    va_list va;
    va_start(va, first_arg_type);
    bool res = dbushelper_send_va(connection, service, path, interface, member,
                                  callback, user_data, user_free,
                                  ppc, first_arg_type, va);
    va_end(va);
    return res;
}

static DBusMessage *
dbushelper_steal_reply(DBusPendingCall *pc)
{
    DBusMessage *ret = 0;
    DBusMessage *rsp = 0;
    DBusError    err = DBUS_ERROR_INIT;

    if (!(rsp = dbus_pending_call_steal_reply(pc))) {
        log_err("got null reply");
    } else if (dbus_set_error_from_message(&err, rsp)) {
        if (equal(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER))
            log_err("got no reply");
        else
            log_err("got error reply: " << err.name << ": " << err.message);
    } else {
        ret = rsp;
        rsp = 0;
    }

    if (rsp)
        dbus_message_unref(rsp);
    dbus_error_free(&err);

    return ret;
}

static bool
dbushelper_parse_args(DBusMessage *msg, int first_arg_type, ...)
{
    bool         ack = false;
    DBusError    err = DBUS_ERROR_INIT;

    va_list va;
    va_start(va, first_arg_type);
    if (!dbus_message_get_args_valist(msg, &err, first_arg_type, va))
        log_err("parse error: " << err.name << ": " << err.message);
    else
        ack = true;
    va_end(va);

    dbus_error_free(&err);

    return ack;
}

/* ========================================================================= *
 * UnitProps
 * ========================================================================= */

struct UnitProps
{
    char *m_activeState;
    char *m_subState;

    UnitProps()
    {
        m_activeState = nullptr;
        m_subState = nullptr;
    }
    ~UnitProps()
    {
        free(m_activeState);
        m_activeState = nullptr;
        free(m_subState);
        m_subState = nullptr;
    }
    const char *activeState() const
    {
        return m_activeState;
    }
    const char *subState() const
    {
        return m_subState;
    }
    void setString(const char *name, char **prev, const char *curr)
    {
        if (!equal(*prev, curr)) {
            log_debug(name << ": " << stringRepr(*prev)
                      << " -> " << stringRepr(curr));
            free(*prev);
            *prev = curr ? strdup(curr) : nullptr;
        }
    }
    void setActiveState(const char *val)
    {
        setString("m_activeState", &m_activeState, val);
    }
    void setSubState(const char *val)
    {
        setString("m_subState", &m_subState, val);
    }
    void updateFrom(DBusMessageIter *iter);
};

void UnitProps::updateFrom(DBusMessageIter *iter)
{
    /* Parse array of added/changed keys
     */
    DBusMessageIter changed;
    if (dbushelper_get_array(iter, &changed)) {
        for (;;) {
            if (dbushelper_at_end(&changed)) {
                /* Parse array of dropped keys
                 *
                 * Note that this is present in change notification
                 * signals, but not in method call reply messages and
                 * thus needs to be treated as optional.
                 */
                DBusMessageIter dropped;
                if (!dbushelper_at_end(iter)) {
                    if (dbushelper_get_array(iter, &dropped)) {
                        while (!dbushelper_at_end(&dropped)) {
                            const char *key = 0;
                            if (!dbushelper_get_string(&dropped, &key))
                                break;
                            if (equal(key, SYSTEMD_MOUNT_PROPERTY_ACTIVE_STATE))
                                setActiveState(0);
                            else if (equal(key, SYSTEMD_MOUNT_PROPERTY_SUB_STATE))
                                setSubState(0);
                        }
                    }
                }
                break;
            }
            DBusMessageIter dentry;
            if (!dbushelper_get_dentry(&changed, &dentry))
                break;
            const char *key = 0;
            if (!dbushelper_get_string(&dentry, &key))
                break;
            DBusMessageIter var;
            if (!dbushelper_get_variant(&dentry, &var))
                break;
            if (equal(key, SYSTEMD_MOUNT_PROPERTY_ACTIVE_STATE)) {
                const char *state = 0;
                if (dbushelper_get_string(&var, &state))
                    setActiveState(state);
            } else if (equal(key, SYSTEMD_MOUNT_PROPERTY_SUB_STATE)) {
                const char *subState = 0;
                if (dbushelper_get_string(&var, &subState))
                    setSubState(subState);
            }
        }
    }
}

/* ========================================================================= *
 * Compositor
 * ========================================================================= */

Compositor::~Compositor()
{
    disconnectFromSystemBus();
    free(m_mceNameOwner);
    delete m_targetUnitProps;
}

Compositor::Compositor(MinUi::EventLoop *eventLoop)
    : m_systemBus(nullptr)
    , m_displayState(DisplayState::Unknown)
    , m_updatesEnabled(-1) // change to true/false => notification
    , m_compositorOwned(false)
    , m_replacementAllowed(false)
    , m_targetUnitActive(false)
    , m_targetUnitProps(new UnitProps)
    , m_mceAvailable(false)
    , m_mceNameOwner(nullptr)
    , m_blankPreventWanted(false)
    , m_blankPreventAllowed(false)
    , m_blankPreventTimer(0)
    , m_eventLoop(eventLoop)
{
    if (!connectToSystemBus()) {
        log_err("no system bus connection; terminating");
        ::abort();
    }

    if (!acquireName()) {
        log_err("compositor name ownership not acquired; terminating");
        ::abort();
    }
}

/* ========================================================================= *
 * compositorOwned
 * ========================================================================= */

bool
Compositor::compositorOwned() const
{
    return m_compositorOwned;
}

void
Compositor::updateCompositorOwned(bool owned)
{
    if (m_compositorOwned != owned) {
        m_compositorOwned = owned;
        log_debug("m_compositorOwned: " << m_compositorOwned);

        if (m_compositorOwned) {
            evaluateNameReplacement();
        } else {
            log_debug("compositor handoff done; exiting");
            m_eventLoop->exit();
        }
    }
}

/* ========================================================================= *
 * mceAvailable
 * ========================================================================= */

void
Compositor::updateMceAvailable(bool available)
{
    if (m_mceAvailable != available) {
        m_mceAvailable = available;

        log_debug("m_mceAvailable: " << m_mceAvailable);

        if (m_mceAvailable) {
            // NB: getting updatesEnabled in sync is handled by mce
            displayStateQuery();
            blankPreventAllowedQuery();
        } else {
            /* Stop blank prevent ping-pong */
            updateBlankPreventAllowed(false);
        }
    }
}

void
Compositor::updateMceNameOwner(const char *owner)
{
    if (owner && !*owner)
        owner = nullptr;

    if (!equal(m_mceNameOwner, owner)) {
        log_debug("m_mceNameOwner: "
                  << stringRepr(m_mceNameOwner) << " ->"
                  << stringRepr(owner));

        /* If we ever see handoff from one owner to another, it needs
         * to be handled as: old_owner -> no_owner -> new_owner.
         */

        free(m_mceNameOwner);
        m_mceNameOwner = nullptr;
        updateMceAvailable(false);

        if (owner) {
            m_mceNameOwner = strdup(owner);
            updateMceAvailable(true);
        }
    }
}

DBusMessage *
Compositor::mceNameOwnerHandler(DBusMessage *msg)
{
    const char *name = 0;
    const char *prev = 0;
    const char *curr = 0;

    if (dbushelper_parse_args(msg,
                              DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_STRING, &prev,
                              DBUS_TYPE_STRING, &curr,
                              DBUS_TYPE_INVALID)) {
        if (equal(name, MCE_SERVICE))
            updateMceNameOwner(curr);
    }

    return nullptr;
}

void
Compositor::mceNameOwnerReply(DBusPendingCall *pc, void *aptr)
{
    Compositor  *self = static_cast<Compositor*>(aptr);
    DBusMessage *rsp  = 0;

    if ((rsp = dbushelper_steal_reply(pc))) {
        const char *curr = 0;
        if (dbushelper_parse_args(rsp,
                                  DBUS_TYPE_STRING, &curr,
                                  DBUS_TYPE_INVALID)) {
            self->updateMceNameOwner(curr);
        }
        dbus_message_unref(rsp);
    }
}

void
Compositor::mceNameOwnerQuery()
{
    const char *arg = MCE_SERVICE;
    dbushelper_send(m_systemBus,
                    DBUS_SERVICE_DBUS,
                    DBUS_PATH_DBUS,
                    DBUS_INTERFACE_DBUS,
                    "GetNameOwner",
                    mceNameOwnerReply,
                    static_cast<void *>(this), 0, 0,
                    DBUS_TYPE_STRING, &arg,
                    DBUS_TYPE_INVALID);
}

/* ========================================================================= *
 * DisplayState
 * ========================================================================= */

void
Compositor::updateDisplayState(const char *state)
{
    DisplayState prev = m_displayState;

    if (!strcmp(state, MCE_DISPLAY_ON_STRING))
        m_displayState = DisplayState::On;
    else if (!strcmp(state, MCE_DISPLAY_DIM_STRING))
        m_displayState = DisplayState::Dim;
    else if (!strcmp(state, MCE_DISPLAY_OFF_STRING))
        m_displayState = DisplayState::Off;
    else
        m_displayState = DisplayState::Unknown;

    if (prev != m_displayState) {
        log_debug("display state: " << m_displayState << " " << state);
        displayStateChanged();
    }
}

DBusMessage *
Compositor::displayStateHandler(DBusMessage *msg)
{
    const char *arg = 0;

    if (dbushelper_parse_args(msg,
                              DBUS_TYPE_STRING, &arg,
                              DBUS_TYPE_INVALID)) {
        updateDisplayState(arg);
    }

    return nullptr;
}

void
Compositor::displayStateReply(DBusPendingCall *pc, void *aptr)
{
    Compositor  *self = static_cast<Compositor*>(aptr);
    DBusMessage *rsp  = 0;

    if ((rsp = dbushelper_steal_reply(pc))) {
        self->displayStateHandler(rsp);
        dbus_message_unref(rsp);
    }
}

void
Compositor::displayStateQuery()
{
    dbushelper_send(m_systemBus,
                    MCE_SERVICE,
                    MCE_REQUEST_PATH,
                    MCE_REQUEST_IF,
                    MCE_DISPLAY_STATUS_GET,
                    displayStateReply,
                    static_cast<void *>(this), 0, 0,
                    DBUS_TYPE_INVALID);
}

/* ========================================================================= *
 * blankPreventAllowed
 * ========================================================================= */

void
Compositor::terminateBlankingPause()
{
    dbushelper_send(m_systemBus,
                    MCE_SERVICE,
                    MCE_REQUEST_PATH,
                    MCE_REQUEST_IF,
                    MCE_CANCEL_PREVENT_BLANK_REQ,
                    0, 0, 0, 0,
                    DBUS_TYPE_INVALID);
}

void
Compositor::requestBlankingPause()
{
    dbushelper_send(m_systemBus,
                    MCE_SERVICE,
                    MCE_REQUEST_PATH,
                    MCE_REQUEST_IF,
                    MCE_PREVENT_BLANK_REQ,
                    0, 0, 0, 0,
                    DBUS_TYPE_INVALID);
}

void
Compositor::evaluateBlankingPause()
{
    /* MCE allows 60 second renew delay. Note that there is no named
     * constant for this value. */
    const int renewDelay = 60 * 1000; // [ms]

    bool preventBlanking = m_blankPreventWanted && m_blankPreventAllowed;

    if (preventBlanking) {
        if (!m_blankPreventTimer) {
            log_debug("blanking pause: start");
            requestBlankingPause();
            m_blankPreventTimer =
                m_eventLoop->createTimer(renewDelay,
                                         [this]() {
                                             log_debug("blanking pause: renew");
                                             requestBlankingPause();
                                         });
        }
    } else {
        if (m_blankPreventTimer) {
            log_debug("blanking pause: stop");
            m_eventLoop->cancelTimer(m_blankPreventTimer),
                m_blankPreventTimer = 0;
            terminateBlankingPause();
        }
    }
}

void
Compositor::setBlankPreventWanted(bool wanted)
{
    if (m_blankPreventWanted != wanted) {
        m_blankPreventWanted = wanted;
        log_debug("m_blankPreventWanted: " << m_blankPreventWanted);
        evaluateBlankingPause();
    }
}

void
Compositor::updateBlankPreventAllowed(bool allowed)
{
    if (m_blankPreventAllowed != allowed) {
        m_blankPreventAllowed = allowed;
        log_debug("m_blankPreventAllowed: " << m_blankPreventAllowed);
        evaluateBlankingPause();
    }
}

DBusMessage *
Compositor::blankPreventAllowedHandler(DBusMessage *msg)
{
    dbus_bool_t arg = false;

    if (dbushelper_parse_args(msg,
                              DBUS_TYPE_BOOLEAN, &arg,
                              DBUS_TYPE_INVALID)) {
        updateBlankPreventAllowed(arg);
    }

    return nullptr;
}

void
Compositor::blankPreventAllowedReply(DBusPendingCall *pc, void *aptr)
{
    Compositor  *self = static_cast<Compositor*>(aptr);
    DBusMessage *rsp  = 0;

    if ((rsp = dbushelper_steal_reply(pc))) {
        self->blankPreventAllowedHandler(rsp);
        dbus_message_unref(rsp);
    }
}

void
Compositor::blankPreventAllowedQuery()
{
    dbushelper_send(m_systemBus,
                    MCE_SERVICE,
                    MCE_REQUEST_PATH,
                    MCE_REQUEST_IF,
                    MCE_PREVENT_BLANK_ALLOWED_GET,
                    blankPreventAllowedReply,
                    static_cast<void *>(this), 0, 0,
                    DBUS_TYPE_INVALID);
}

/* ========================================================================= *
 * UpdatesEnabled
 * ========================================================================= */

void
Compositor::updateUpdatesEnabled(bool state)
{
    int prev = m_updatesEnabled;
    m_updatesEnabled = state;

    if (prev != m_updatesEnabled) {
        log_debug("updates enabled: " << m_updatesEnabled);
        updatesEnabledChanged();
    }
}

DBusMessage *
Compositor::updatesEnabledHandler(DBusMessage *msg)
{
    dbus_bool_t arg = false;

    /* Note: We always need to react to updatesEnabled changes. If there
     *       any problems, the safest thing we can do is to: Act as if
     *       updates were disabled and always send a reply message to
     *       unblock mce side state machinery.
     */

    if (!dbushelper_parse_args(msg,
                               DBUS_TYPE_BOOLEAN, &arg,
                               DBUS_TYPE_INVALID)) {
        log_err("updates enabled parse error");
    }

    updateUpdatesEnabled(arg);

    return dbus_message_new_method_return(msg);
}

DBusMessage *
Compositor::nameLostHandler(DBusMessage *msg)
{
    const char *arg = 0;

    if (dbushelper_parse_args(msg,
                              DBUS_TYPE_STRING, &arg,
                              DBUS_TYPE_INVALID)) {
        log_debug("name lost: " << arg);
        if (equal(arg, COMPOSITOR_SERVICE)) {
            updateCompositorOwned(false);
        }
    }

    return nullptr;
}

DBusMessage *
Compositor::nameAcquiredHandler(DBusMessage *msg)
{
    const char *arg = 0;

    if (dbushelper_parse_args(msg,
                              DBUS_TYPE_STRING, &arg,
                              DBUS_TYPE_INVALID)) {
        log_debug("name acquired: " << arg);
        if (equal(arg, COMPOSITOR_SERVICE)) {
            updateCompositorOwned(true);
        }
    }

    return nullptr;
}

/* ========================================================================= *
 * targetUnitActive
 * ========================================================================= */

bool
Compositor::targetUnitActive() const
{
    return m_targetUnitActive;
}

void
Compositor::updateTargetUnitActive(bool active)
{
    if (m_targetUnitActive != active) {
        m_targetUnitActive = active;
        log_debug("m_targetUnitActive: " << m_targetUnitActive);
        evaluateNameReplacement();
    }
}

void
Compositor::evaluateTargetUnitActive()
{
    bool active = false;
    const char *activeState = m_targetUnitProps->activeState();
    if (equal(activeState, "active")) {
        const char *subState = m_targetUnitProps->subState();
        active = equal(subState, "running") || equal(subState, "mounted");
    }
    updateTargetUnitActive(active);
}

DBusMessage *
Compositor::targetUnitPropsHandler(DBusMessage *msg)
{
    const char *interface = 0;
    DBusMessageIter iter;
    if (dbus_message_iter_init(msg, &iter)) {
        if (dbushelper_get_string(&iter, &interface)) {
            log_debug("property change: " << interface);
            if (equal(interface, SYSTEMD_UNIT_INTERFACE)) {
                m_targetUnitProps->updateFrom(&iter);
                evaluateTargetUnitActive();
            }
        }
    }
    return nullptr;
}

void
Compositor::targetUnitPropsReply(DBusPendingCall *pc, void *aptr)
{
    log_debug("property reply");

    Compositor  *self = static_cast<Compositor*>(aptr);
    DBusMessage *rsp  = 0;
    if ((rsp = dbushelper_steal_reply(pc))) {
        DBusMessageIter iter;
        if (dbus_message_iter_init(rsp, &iter)) {
            self->m_targetUnitProps->updateFrom(&iter);
            self->evaluateTargetUnitActive();
        }
        dbus_message_unref(rsp);
    }
}

void
Compositor::targetUnitPropsQuery()
{
    log_debug("property query");

    const char *arg = SYSTEMD_UNIT_INTERFACE;
    dbushelper_send(m_systemBus,
                    SYSTEMD_SERVICE,
                    SYSTEMD_TARGET_UNIT_PATH,
                    DBUS_PROPERTIES_INTERFACE,
                    DBUS_PROPERTIES_METHOD_GET_ALL,
                    targetUnitPropsReply,
                    static_cast<void *>(this), 0, 0,
                    DBUS_TYPE_STRING, &arg,
                    DBUS_TYPE_INVALID);
}

/* ========================================================================= *
 * D-Bus Connection
 * ========================================================================= */

Compositor::Handler Compositor::s_systemBusHandlers[] =
{
    /* Signal handlers */
    {
        &Compositor::mceNameOwnerHandler,
        nullptr,
        DBUS_PATH_DBUS,
        DBUS_INTERFACE_DBUS,
        "NameOwnerChanged",
        MCE_SERVICE,
        nullptr,
        nullptr,
        false,
    },
    {
        &Compositor::nameLostHandler,
        nullptr,
        DBUS_PATH_DBUS,
        DBUS_INTERFACE_DBUS,
        "NameLost",
        nullptr,
        nullptr,
        nullptr,
        true,
    },
    {
        &Compositor::nameAcquiredHandler,
        nullptr,
        DBUS_PATH_DBUS,
        DBUS_INTERFACE_DBUS,
        "NameAcquired",
        nullptr,
        nullptr,
        nullptr,
        true,
    },
    {
        &Compositor::displayStateHandler,
        nullptr,
        MCE_SIGNAL_PATH,
        MCE_SIGNAL_IF,
        MCE_DISPLAY_SIG,
        nullptr,
        nullptr,
        nullptr,
        false,
    },
    {
        &Compositor::blankPreventAllowedHandler,
        nullptr,
        MCE_SIGNAL_PATH,
        MCE_SIGNAL_IF,
        MCE_PREVENT_BLANK_ALLOWED_SIG,
        nullptr,
        nullptr,
        nullptr,
        false,
    },

    {
        &Compositor::targetUnitPropsHandler,
        nullptr,
        SYSTEMD_TARGET_UNIT_PATH,
        DBUS_PROPERTIES_INTERFACE,
        DBUS_PROPERTIES_SIGNAL_CHANGED,
        SYSTEMD_UNIT_INTERFACE,
        nullptr,
        nullptr,
        false,
    },

    /* Method call handlers */
    {
        &Compositor::updatesEnabledHandler,
        COMPOSITOR_SERVICE,
        COMPOSITOR_PATH,
        COMPOSITOR_IFACE,
        COMPOSITOR_SET_UPDATES_ENABLED,
        nullptr,
        nullptr,
        nullptr,
        false,
    },
    /* Sentinel */
    {
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        false,
    }
};

DBusHandlerResult
Compositor::systemBusFilter(DBusConnection *con, DBusMessage *msg, void *aptr)
{
    Compositor *self = static_cast<Compositor*>(aptr);
    bool handled = false;
    int msgType = dbus_message_get_type(msg);
    if (msgType == DBUS_MESSAGE_TYPE_METHOD_CALL || msgType == DBUS_MESSAGE_TYPE_SIGNAL) {
        const char *interface = dbus_message_get_interface(msg);
        const char *object    = dbus_message_get_path(msg);
        const char *member    = dbus_message_get_member(msg);
        if (interface && object && member) {
            const char *service = nullptr;
            if (msgType == DBUS_MESSAGE_TYPE_METHOD_CALL)
                service = dbus_message_get_destination(msg) ?: "";
            for (size_t i = 0; !handled && s_systemBusHandlers[i].callback; ++i) {
                if (!equal(s_systemBusHandlers[i].service, service) ||
                    !equal_or_dontcare(s_systemBusHandlers[i].interface, interface) ||
                    !equal_or_dontcare(s_systemBusHandlers[i].object, object) ||
                    !equal_or_dontcare(s_systemBusHandlers[i].member, member))
                    continue;
                log_debug("handle: " << interface << "." << member << "()");
                DBusMessage *rsp = (self->*s_systemBusHandlers[i].callback)(msg);
                if (service) {
                    handled = true;
                    if (!dbus_message_get_no_reply(msg)) {
                        if (!rsp)
                            rsp = dbus_message_new_error(msg, DBUS_ERROR_FAILED,
                                                         member);
                        if (!rsp || !dbus_connection_send(con, rsp, 0))
                            log_err("failed to send reply");
                    }
                }
                if (rsp)
                    dbus_message_unref(rsp);
            }
        }
    }
    return (handled
            ? DBUS_HANDLER_RESULT_HANDLED
            : DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}

bool
Compositor::generateMatch(Compositor::Handler &handler, char *buff, size_t size)
{
    char interface[64] = "";
    char object[64] = "";
    char member[64] = "";
    char arg0[64] = "";
    char arg1[64] = "";
    char arg2[64] = "";

    /* Skip method call handlers */
    if (handler.service != 0)
        return false;

    /* Skip implicitly sent signals such as NameAcquired/NameLost */
    if (handler.implicit)
        return false;

    if (handler.interface)
        snprintf(interface, sizeof interface, ",interface='%s'",
                 handler.interface);

    if (handler.object)
        snprintf(object, sizeof object, ",path='%s'",
                 handler.object);

    if (handler.member)
        snprintf(member, sizeof member, ",member='%s'",
                 handler.member);

    if (handler.arg0)
        snprintf(arg0, sizeof arg0, ",arg0='%s'",
                 handler.arg0);

    if (handler.arg1)
        snprintf(arg1, sizeof arg1, ",arg1='%s'",
                 handler.arg1);

    if (handler.arg2)
        snprintf(arg2, sizeof arg2, ",arg2='%s'",
                 handler.arg2);

    snprintf(buff, size, "type=signal%s%s%s%s%s%s",
             interface, object, member,
             arg0, arg1, arg2);

    return true;
}

void
Compositor::addSystemBusMatches()
{
    for (size_t i = 0; s_systemBusHandlers[i].callback; ++i) {
        char match[256];
        if (generateMatch(s_systemBusHandlers[i], match, sizeof match)) {
            log_debug("add match: " << match);
            dbus_bus_add_match(m_systemBus, match, 0);
        }
    }
}

void
Compositor::removeSystemBusMatches()
{
    for (size_t i = 0; s_systemBusHandlers[i].callback; ++i) {
        char match[256];
        if (generateMatch(s_systemBusHandlers[i], match, sizeof match)) {
            log_debug("remove match: " << match);
            dbus_bus_remove_match(m_systemBus, match, 0);
        }
    }
}

void
Compositor::subscribeSystemdNotifications() const
{
    log_debug("subscribe: call");
    dbushelper_send(m_systemBus,
                    SYSTEMD_SERVICE,
                    SYSTEMD_MANAGER_PATH,
                    SYSTEMD_MANAGER_INTERFACE,
                    SYSTEMD_MANAGER_METHOD_SUBSCRIBE,
                    [](DBusPendingCall *pc, void *aptr) {
                        (void)aptr;
                        DBusMessage *rsp = 0;
                        if ((rsp = dbushelper_steal_reply(pc))) {
                            log_debug("subscribe: reply");
                            dbus_message_unref(rsp);
                        }
                    },
                    0, 0, 0,
                    DBUS_TYPE_INVALID);
}

bool
Compositor::evaluateNameReplacement(void)
{
    if (!m_replacementAllowed && compositorOwned() && targetUnitActive()) {
        DBusError err = DBUS_ERROR_INIT;
        unsigned flags = (DBUS_NAME_FLAG_DO_NOT_QUEUE |
                          DBUS_NAME_FLAG_ALLOW_REPLACEMENT);
        int rc = dbus_bus_request_name(m_systemBus,
                                       COMPOSITOR_SERVICE,
                                       flags, &err);
        if (dbus_error_is_set(&err)) {
            log_err("failed to adjust dbus name:"
                    << err.name << ": " <<  err.message);
        } else if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER &&
                   rc != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER) {
            log_err("failed to adjust dbus name: rc=" << rc);
        } else {
            log_debug("compositor handoff allowed");
            m_replacementAllowed = true;
        }
        dbus_error_free(&err);
    }

    if (targetUnitActive() && !m_replacementAllowed) {
        log_err("compositor handoff not possible; exit immediately");
        m_eventLoop->exit();
    }

    return m_replacementAllowed;
}

bool
Compositor::acquireName()
{
    DBusError err = DBUS_ERROR_INIT;
    unsigned flags = (DBUS_NAME_FLAG_DO_NOT_QUEUE |
                      DBUS_NAME_FLAG_REPLACE_EXISTING);
    int rc = dbus_bus_request_name(m_systemBus,
                                   COMPOSITOR_SERVICE,
                                   flags, &err);
    if (dbus_error_is_set(&err)) {
        log_err("failed to obtain dbus name:"
                << err.name << ": " <<  err.message);
    } else if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        log_err("failed to obtain dbus name");
    } else {
        updateCompositorOwned(true);
        addSystemBusMatches();
        subscribeSystemdNotifications();
        mceNameOwnerQuery();
        targetUnitPropsQuery();
    }
    dbus_error_free(&err);

    return compositorOwned();
}

bool
Compositor::connectToSystemBus()
{
    if (!m_systemBus) {
        log_debug("dbus connect");
        DBusError err = DBUS_ERROR_INIT;
        if (!(m_systemBus = dbus_bus_get(DBUS_BUS_SYSTEM, &err))) {
            log_err("system bus connect failed: "
                    << err.name << ": " <<  err.message);
        } else {
            dbus_connection_add_filter(m_systemBus, systemBusFilter,
                                       static_cast<void*>(this), 0);
        }
        dbus_error_free(&err);
    }

    return m_systemBus != 0;
}

void
Compositor::disconnectFromSystemBus()
{
    if (m_systemBus) {
        log_debug("dbus disconnect");

        dbus_connection_remove_filter(m_systemBus, systemBusFilter,
                                      static_cast<void*>(this));

        if (dbus_connection_get_is_connected(m_systemBus)) {
            removeSystemBusMatches();

            /* Note: Name is left to be released implicitly
             *       when we get disconnected at exit, or
             *       we are exiting because it was already
             *       taken from us via ownership replacement.
             */
        }

        dbus_connection_unref(m_systemBus),
            m_systemBus = nullptr;
    }
}
};
