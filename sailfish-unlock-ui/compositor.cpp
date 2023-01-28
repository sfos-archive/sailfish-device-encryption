/*
 * Copyright (c) 2019 - 2023 Jolla Ltd.
 *
 * License: Proprietary
 */

#include "compositor.h"
#include "logging.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#include <dsme/dsme_dbus_if.h>
#include <sailfish-minui/eventloop.h>

#define COMPOSITOR_SERVICE "org.nemomobile.compositor"
#define COMPOSITOR_PATH "/"
#define COMPOSITOR_INTERFACE "org.nemomobile.compositor"
#define COMPOSITOR_METHOD_SET_UPDATES_ENABLED "setUpdatesEnabled"
#define COMPOSITOR_METHOD_GET_TOPMOST_WINDOW_PID "privateTopmostWindowProcessId"
#define COMPOSITOR_METHOD_GET_SETUP_ACTIONS "privateGetSetupActions"
#define COMPOSITOR_SIGNAL_TOPMOST_WINDOW_PID_CHANGED "privateTopmostWindowProcessIdChanged"

#define COMPOSITOR_ACTION_NONE 0
#define COMPOSITOR_ACTION_STOP_HWC (1<<0)
#define COMPOSITOR_ACTION_START_HWC (1<<1)
#define COMPOSITOR_ACTION_RESTART_HWC (1<<2)

#define SYSTEMD_SERVICE "org.freedesktop.systemd1"
#define SYSTEMD_MANAGER_PATH "/org/freedesktop/systemd1"
#define SYSTEMD_MANAGER_INTERFACE "org.freedesktop.systemd1.Manager"
#define SYSTEMD_MANAGER_METHOD_LIST_UNITS "ListUnits"
#define SYSTEMD_MANAGER_METHOD_SUBSCRIBE "Subscribe"
#define SYSTEMD_MANAGER_METHOD_UNSUBSCRIBE "Unsubscribe"

#define SYSTEMD_TARGET_UNIT_PATH "/org/freedesktop/systemd1/unit/home_2emount"
#define SYSTEMD_UNIT_INTERFACE "org.freedesktop.systemd1.Unit"
#define SYSTEMD_UNIT_PROPERTY_ACTIVE_STATE "ActiveState"
#define SYSTEMD_UNIT_PROPERTY_SUB_STATE "SubState"

#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"
#define DBUS_PROPERTIES_METHOD_GET "Get"
#define DBUS_PROPERTIES_METHOD_GET_ALL "GetAll"
#define DBUS_PROPERTIES_METHOD_SET "Set"
#define DBUS_PROPERTIES_SIGNAL_CHANGED "PropertiesChanged"

/* While testing/debugging, it can be easier to start/stop
 * for example mce service rather than mount/unmount /home ...
 */
#if 0
#undef SYSTEMD_TARGET_UNIT_PATH
#define SYSTEMD_TARGET_UNIT_PATH "/org/freedesktop/systemd1/unit/mce_2eservice"
#endif

namespace Sailfish {
#if LOGGING_ENABLE_DEBUG
static const char *stringRepr(const char *stringOrNull)
{
    if (!stringOrNull)
        return "<null>";
    if (!*stringOrNull)
        return "<empty>";
    return stringOrNull;
}
#endif

static bool stringsAreEqual(const char *patternString, const char *stringToMatch)
{
    if (!patternString) {
        if (!stringToMatch)
            return true;
        return false;
    }
    if (!stringToMatch)
        return false;
    return !strcmp(patternString, stringToMatch);
}

static bool stringsAreEqualOrWeDoNotCare(const char *patternString, const char *stringToMatch)
{
    if (!patternString)
        return true;
    if (!stringToMatch)
        return false;
    return !strcmp(patternString, stringToMatch);
}

static const struct {
    Compositor::DsmeState m_dsmeState;
    const char *m_dsmeStateName;
} dsmeStateLookupTable[] = {
#define DSME_STATE(STATE, VALUE) { Compositor::DSME_STATE_##STATE, #STATE },
#include <dsme/state_states.h>
#undef DSME_STATE
};

static Compositor::DsmeState lookupDsmeStateValue(const char *dsmeStateName)
{
    Compositor::DsmeState dsmeStateValue = Compositor::DSME_STATE_NOT_SET;
    for (size_t i = 0; i < sizeof dsmeStateLookupTable / sizeof *dsmeStateLookupTable; ++i) {
        if (stringsAreEqual(dsmeStateLookupTable[i].m_dsmeStateName, dsmeStateName)) {
            dsmeStateValue = dsmeStateLookupTable[i].m_dsmeState;
            break;
        }
    }
    return dsmeStateValue;
}

#if LOGGING_ENABLE_DEBUG
static const char *lookupDsmeStateName(Compositor::DsmeState dsmeState)
{
    const char *dsmeStateName = "UNKNOWN";
    for (size_t i = 0; i < sizeof dsmeStateLookupTable / sizeof *dsmeStateLookupTable; ++i) {
        if (dsmeStateLookupTable[i].m_dsmeState == dsmeState) {
            dsmeStateName = dsmeStateLookupTable[i].m_dsmeStateName;
            break;
        }
    }
    return dsmeStateName;
}
#endif

static const char *lookupDbusTypeName(int type)
{
    switch (type) {
    case DBUS_TYPE_INVALID:
        return "INVALID";
    case DBUS_TYPE_BYTE:
        return "BYTE";
    case DBUS_TYPE_BOOLEAN:
        return "BOOLEAN";
    case DBUS_TYPE_INT16:
        return "INT16";
    case DBUS_TYPE_UINT16:
        return "UINT16";
    case DBUS_TYPE_INT32:
        return "INT32";
    case DBUS_TYPE_UINT32:
        return "UINT32";
    case DBUS_TYPE_INT64:
        return "INT64";
    case DBUS_TYPE_UINT64:
        return "UINT64";
    case DBUS_TYPE_DOUBLE:
        return "DOUBLE";
    case DBUS_TYPE_STRING:
        return "STRING";
    case DBUS_TYPE_OBJECT_PATH:
        return "OBJECT_PATH";
    case DBUS_TYPE_SIGNATURE:
        return "SIGNATURE";
    case DBUS_TYPE_UNIX_FD:
        return "UNIX_FD";
    case DBUS_TYPE_ARRAY:
        return "ARRAY";
    case DBUS_TYPE_VARIANT:
        return "VARIANT";
    case DBUS_TYPE_STRUCT:
        return "STRUCT";
    case DBUS_TYPE_DICT_ENTRY:
        return "DICT_ENTRY";
    default:
        return "UNKNOWN";
    }
}

static int getTypeAtMessageIter(DBusMessageIter *messageIter)
{
    return dbus_message_iter_get_arg_type(messageIter);
}

static bool dbusIteratorHasNoMoreData(DBusMessageIter *messageIter)
{
    return getTypeAtMessageIter(messageIter) == DBUS_TYPE_INVALID;
}

static bool requireTypeAtMessageIter(DBusMessageIter *messageIter, int requiredType)
{
    int typeAtIterator = getTypeAtMessageIter(messageIter);
    if (typeAtIterator == requiredType)
        return true;
    log_err("dbus message parse error: expected "
            << lookupDbusTypeName(requiredType) << ", got "
            << lookupDbusTypeName(typeAtIterator));
    return false;
}

static bool parseStringValueFromMessageIter(DBusMessageIter *messageIter, const char **valuePointer)
{
    if (!requireTypeAtMessageIter(messageIter, DBUS_TYPE_STRING))
        return false;
    const char *stringData = 0;
    dbus_message_iter_get_basic(messageIter, &stringData);
    dbus_message_iter_next(messageIter);
    *valuePointer = stringData;
    return true;
}

static void parseStringArgumentFromMessageIter(DBusMessageIter *messageIter, const char **valuePointer)
{
    const char *stringData = 0;
    switch (getTypeAtMessageIter(messageIter)) {
    case DBUS_TYPE_STRING:
        dbus_message_iter_get_basic(messageIter, &stringData);
        /* Fall through */
    default:
        dbus_message_iter_next(messageIter);
        /* Fall through */
    case DBUS_TYPE_INVALID:
        break;
    }
    *valuePointer = stringData;
}

static bool parseVariantContainerFromMessageIter(DBusMessageIter *messageIter, DBusMessageIter *containerIter)
{
    if (!requireTypeAtMessageIter(messageIter, DBUS_TYPE_VARIANT))
        return false;
    dbus_message_iter_recurse(messageIter, containerIter);
    dbus_message_iter_next(messageIter);
    return true;
}

static bool parseArrayContainerFromMessageIter(DBusMessageIter *messageIter, DBusMessageIter *containerIter)
{
    if (!requireTypeAtMessageIter(messageIter, DBUS_TYPE_ARRAY))
        return false;
    dbus_message_iter_recurse(messageIter, containerIter);
    dbus_message_iter_next(messageIter);
    return true;
}

static bool parseDictEntryContainerFromMessageIter(DBusMessageIter *messageIter, DBusMessageIter *containerIter)
{
    if (!requireTypeAtMessageIter(messageIter, DBUS_TYPE_DICT_ENTRY))
        return false;
    dbus_message_iter_recurse(messageIter, containerIter);
    dbus_message_iter_next(messageIter);
    return true;
}

static bool sendDbusMethodCallMessageVaList(DBusConnection *dbusConnection, const char *serviceName, const char *objectPath,
                                            const char *interfaceName, const char *memberName,
                                            DBusPendingCallNotifyFunction replyNotifyFunction, void *userDataPointer,
                                            DBusFreeFunction userDataFreeFunction, DBusPendingCall **optionalPendingCallResult,
                                            int firstArgumentType, va_list vaList)
{
    log_debug("send: " << serviceName << ": " << interfaceName << "." << memberName);
    bool messageSuccesfullySent = false;
    DBusMessage *methodCallMessage = 0;
    if ((methodCallMessage = dbus_message_new_method_call(serviceName, objectPath, interfaceName, memberName))) {
        if (firstArgumentType != DBUS_TYPE_INVALID && !dbus_message_append_args_valist(methodCallMessage, firstArgumentType, vaList)) {
            log_err("failed to append arguments to D-Bus message: "
                    << interfaceName << "." <<  memberName);
        } else if (!replyNotifyFunction) {
            dbus_message_set_no_reply(methodCallMessage, true);
            if (dbus_connection_send(dbusConnection, methodCallMessage, 0))
                messageSuccesfullySent = true;
        } else {
            const int timeoutInMilliseconds = DBUS_TIMEOUT_USE_DEFAULT;
            DBusPendingCall *pendingCall = 0;
            if (dbus_connection_send_with_reply(dbusConnection, methodCallMessage, &pendingCall, timeoutInMilliseconds)) {
                if (pendingCall) {
                    if (dbus_pending_call_set_notify(pendingCall, replyNotifyFunction, userDataPointer, userDataFreeFunction)) {
                        userDataPointer = 0;
                        userDataFreeFunction = 0;
                        if (optionalPendingCallResult)
                            *optionalPendingCallResult = dbus_pending_call_ref(pendingCall);
                        messageSuccesfullySent = true;
                    }
                    dbus_pending_call_unref(pendingCall);
                }
            }
        }
        dbus_message_unref(methodCallMessage);
    }
    if (userDataFreeFunction)
        userDataFreeFunction(userDataPointer);
    return messageSuccesfullySent;
}

static bool sendDbusMethodCallMessage(DBusConnection *dbusConnection, const char *serviceName, const char *objectPath,
                                      const char *interfaceName, const char *memberName,
                                      DBusPendingCallNotifyFunction replyNotifyFunction, void *userDataPointer,
                                      DBusFreeFunction userDataFreeFunction, DBusPendingCall **optionalPendingCallResult,
                                      int firstArgumentType, ...)
{
    va_list vaList;
    va_start(vaList, firstArgumentType);
    bool messageSuccesfullySent = sendDbusMethodCallMessageVaList(dbusConnection, serviceName, objectPath, interfaceName, memberName,
                                                                  replyNotifyFunction, userDataPointer, userDataFreeFunction,
                                                                  optionalPendingCallResult, firstArgumentType, vaList);
    va_end(vaList);
    return messageSuccesfullySent;
}

static DBusMessage *getReplyMessageFromPendingCall(DBusPendingCall *pendingCall)
{
    DBusMessage *replyMessageToReturn = 0;
    DBusMessage *replyMessageCandidate = 0;
    DBusError dbusError = DBUS_ERROR_INIT;
    if (!(replyMessageCandidate = dbus_pending_call_steal_reply(pendingCall))) {
        log_err("got null reply");
    } else if (dbus_set_error_from_message(&dbusError, replyMessageCandidate)) {
        if (!stringsAreEqual(dbusError.name, DBUS_ERROR_NAME_HAS_NO_OWNER))
            log_err("got error reply: " << dbusError.name << ": " << dbusError.message);
    } else {
        replyMessageToReturn = replyMessageCandidate;
        replyMessageCandidate = 0;
    }
    if (replyMessageCandidate)
        dbus_message_unref(replyMessageCandidate);
    dbus_error_free(&dbusError);
    return replyMessageToReturn;
}

static bool parseArgumentsFromDbusMessage(DBusMessage *dbusMessage, int firstArgumentType, ...)
{
    bool successfullyCompleted = false;
    DBusError dbusError = DBUS_ERROR_INIT;
    va_list vaList;
    va_start(vaList, firstArgumentType);
    if (!dbus_message_get_args_valist(dbusMessage, &dbusError, firstArgumentType, vaList))
        log_err("parse error: " << dbusError.name << ": " << dbusError.message);
    else
        successfullyCompleted = true;
    va_end(vaList);
    dbus_error_free(&dbusError);
    return successfullyCompleted;
}

class TargetUnitProperties
{
public:
    TargetUnitProperties()
    {
        m_activeState = nullptr;
        m_subState = nullptr;
    }
    ~TargetUnitProperties()
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
    void updateFromMessageIter(DBusMessageIter *messageIter);
private:
    void setStringValue(const char *keyName, char **pointerToPreviousValue, const char *currentValue)
    {
        (void)keyName; /* unused if debug logging is disabled */
        if (!stringsAreEqual(*pointerToPreviousValue, currentValue)) {
            log_debug(keyName << ": " << stringRepr(*pointerToPreviousValue)
                      << " -> " << stringRepr(currentValue));
            free(*pointerToPreviousValue);
            *pointerToPreviousValue = currentValue ? strdup(currentValue) : nullptr;
        }
    }
    void setActiveState(const char *activeState)
    {
        setStringValue("m_activeState", &m_activeState, activeState);
    }
    void setSubState(const char *subState)
    {
        setStringValue("m_subState", &m_subState, subState);
    }
    char *m_activeState;
    char *m_subState;
};

void TargetUnitProperties::updateFromMessageIter(DBusMessageIter *messageIter)
{
    /* Parse array of added/changed keys
     */
    DBusMessageIter changedValuesArrayIter;
    if (parseArrayContainerFromMessageIter(messageIter, &changedValuesArrayIter)) {
        for (;;) {
            if (dbusIteratorHasNoMoreData(&changedValuesArrayIter)) {
                /* End of added/changed keys reached
                 *
                 * Parse array of dropped keys
                 *
                 * Note that this is present in change notification
                 * signals, but not in method call reply messages and
                 * thus needs to be treated as optional.
                 */
                DBusMessageIter droppedValuesArrayIter;
                if (!dbusIteratorHasNoMoreData(messageIter)) {
                    if (parseArrayContainerFromMessageIter(messageIter, &droppedValuesArrayIter)) {
                        while (!dbusIteratorHasNoMoreData(&droppedValuesArrayIter)) {
                            const char *propertyName = 0;
                            if (!parseStringValueFromMessageIter(&droppedValuesArrayIter, &propertyName))
                                break;
                            if (stringsAreEqual(propertyName, SYSTEMD_UNIT_PROPERTY_ACTIVE_STATE))
                                setActiveState(0);
                            else if (stringsAreEqual(propertyName, SYSTEMD_UNIT_PROPERTY_SUB_STATE))
                                setSubState(0);
                        }
                    }
                }
                break;
            }
            DBusMessageIter dictEntryIter;
            if (!parseDictEntryContainerFromMessageIter(&changedValuesArrayIter, &dictEntryIter))
                break;
            const char *propertyName = 0;
            if (!parseStringValueFromMessageIter(&dictEntryIter, &propertyName))
                break;
            DBusMessageIter variantIter;
            if (!parseVariantContainerFromMessageIter(&dictEntryIter, &variantIter))
                break;
            if (stringsAreEqual(propertyName, SYSTEMD_UNIT_PROPERTY_ACTIVE_STATE)) {
                const char *activeState = 0;
                if (parseStringValueFromMessageIter(&variantIter, &activeState))
                    setActiveState(activeState);
            } else if (stringsAreEqual(propertyName, SYSTEMD_UNIT_PROPERTY_SUB_STATE)) {
                const char *subState = 0;
                if (parseStringValueFromMessageIter(&variantIter, &subState))
                    setSubState(subState);
            }
        }
    }
}

Compositor::~Compositor()
{
    disconnectFromSystemBus();
    free(m_mceNameOwner);
    m_mceNameOwner = nullptr;
    delete m_targetUnitProperties;
    m_targetUnitProperties = nullptr;
}

Compositor::Compositor(MinUi::EventLoop *eventLoop)
    : m_eventLoop(eventLoop)
    , m_systemBusConnection(nullptr)
    , m_targetUnitActive(false)
    , m_targetUnitProperties(new TargetUnitProperties)
    , m_dsmeNameOwner(nullptr)
    , m_dsmeIsRunning(false)
    , m_dsmeState(DSME_STATE_NOT_SET)
    , m_compositorNameOwned(false)
    , m_replacingCompositorNameOwnerAllowed(false)
    , m_updatesState(UpdatesState::UpdatesUnknown)
    , m_mceNameOwner(nullptr)
    , m_mceIsRunning(false)
    , m_blankPreventWanted(false)
    , m_blankPreventAllowed(false)
    , m_blankPreventRenewTimer(0)
    , m_batteryLevel(MCE_BATTERY_LEVEL_UNKNOWN)
    , m_batteryStatus(BatteryStatus::BatteryUnknown)
    , m_chargerState(ChargerState::ChargerUnknown)
    , m_displayState(DisplayState::DisplayUnknown)
{
    if (!connectToSystemBus()) {
        log_err("no system bus connection; terminating");
        ::abort();
    }
    if (!sendCompositorNameOwnershipRequestToDbusDaemon()) {
        log_err("compositor name ownership not acquired; terminating");
        ::abort();
    }
}

Compositor::DisplayState Compositor::displayState() const
{
    return m_displayState;
}

void Compositor::displayStateChanged()
{
    /* This is a stub virtual function */
    log_debug("ignoring: display state change");
}

void Compositor::updateInternallyCachedDisplayState(const char *displayStateName)
{
    DisplayState previousDisplayState = m_displayState;
    if (!strcmp(displayStateName, MCE_DISPLAY_ON_STRING))
        m_displayState = DisplayState::DisplayOn;
    else if (!strcmp(displayStateName, MCE_DISPLAY_DIM_STRING))
        m_displayState = DisplayState::DisplayDim;
    else if (!strcmp(displayStateName, MCE_DISPLAY_OFF_STRING))
        m_displayState = DisplayState::DisplayOff;
    else
        m_displayState = DisplayState::DisplayUnknown;
    if (previousDisplayState != m_displayState) {
        log_debug("display state: " << m_displayState << " " << displayStateName);
        displayStateChanged();
    }
}

void Compositor::handleDisplayStateMessageFromMce(DBusMessage *signalOrReplyMessage)
{
    const char *displayStateName = 0;
    if (parseArgumentsFromDbusMessage(signalOrReplyMessage, DBUS_TYPE_STRING, &displayStateName, DBUS_TYPE_INVALID))
        updateInternallyCachedDisplayState(displayStateName);
}

void Compositor::handleDisplayStateSignalFromMce(DBusMessage *signalMessage)
{
    handleDisplayStateMessageFromMce(signalMessage);
}

void Compositor::handleDisplayStateReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        thisAsUserDataPointer->handleDisplayStateMessageFromMce(replyMessage);
        dbus_message_unref(replyMessage);
    }
}

void Compositor::sendDisplayStateQueryToMce()
{
    sendDbusMethodCallMessage(m_systemBusConnection, MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF,
                              MCE_DISPLAY_STATUS_GET, handleDisplayStateReplyFromMce,
                              static_cast<void *>(this), 0, 0, DBUS_TYPE_INVALID);
}

Compositor::ChargerState Compositor::chargerState() const
{
    return m_chargerState;
}

void Compositor::chargerStateChanged()
{
    /* This is a stub virtual function */
    log_debug("ignoring: charger state change");
}

void Compositor::updateInternallyCachedChargerState(const char *chargerStateName)
{
    ChargerState previousChargerState = m_chargerState;
    if (!strcmp(chargerStateName, MCE_CHARGER_STATE_ON))
        m_chargerState = ChargerState::ChargerOn;
    else if (!strcmp(chargerStateName, MCE_CHARGER_STATE_OFF))
        m_chargerState = ChargerState::ChargerOff;
    else
        m_chargerState = ChargerState::ChargerUnknown;
    if (previousChargerState != m_chargerState) {
        log_debug("m_chargerState: " << m_chargerState << " " << chargerStateName);
        chargerStateChanged();
    }
}

void Compositor::handleChargerStateMessageFromMce(DBusMessage *signalOrReplyMessage)
{
    const char *chargerStateName = 0;
    if (parseArgumentsFromDbusMessage(signalOrReplyMessage, DBUS_TYPE_STRING, &chargerStateName, DBUS_TYPE_INVALID))
        updateInternallyCachedChargerState(chargerStateName);
}

void Compositor::handleChargerStateSignalFromMce(DBusMessage *signalMessage)
{
    handleChargerStateMessageFromMce(signalMessage);
}

void Compositor::handleChargerStateReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        thisAsUserDataPointer->handleChargerStateMessageFromMce(replyMessage);
        dbus_message_unref(replyMessage);
    }
}

void Compositor::sendChargerStateQueryToMce()
{
    log_debug("charger state: query");
    sendDbusMethodCallMessage(m_systemBusConnection, MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF,
                              MCE_CHARGER_STATE_GET, handleChargerStateReplyFromMce,
                              static_cast<void *>(this), 0, 0, DBUS_TYPE_INVALID);
}

Compositor::BatteryStatus Compositor::batteryStatus() const
{
    return m_batteryStatus;
}

void Compositor::batteryStatusChanged()
{
    /* This is a stub virtual function */
    log_debug("ignoring: battery status change");
}

void Compositor::updateInternallyCachedBatteryStatus(const char *batteryStatusName)
{
    BatteryStatus previousBatteryStatus = m_batteryStatus;
    if (!strcmp(batteryStatusName, MCE_BATTERY_STATUS_FULL))
        m_batteryStatus = BatteryStatus::BatteryFull;
    else if (!strcmp(batteryStatusName, MCE_BATTERY_STATUS_OK))
        m_batteryStatus = BatteryStatus::BatteryOk;
    else if (!strcmp(batteryStatusName, MCE_BATTERY_STATUS_LOW))
        m_batteryStatus = BatteryStatus::BatteryLow;
    else if (!strcmp(batteryStatusName, MCE_BATTERY_STATUS_EMPTY))
        m_batteryStatus = BatteryStatus::BatteryEmpty;
    else
        m_batteryStatus = BatteryStatus::BatteryUnknown;
    if (previousBatteryStatus != m_batteryStatus) {
        log_debug("m_batteryStatus: " << m_batteryStatus << " " << batteryStatusName);
        batteryStatusChanged();
    }
}

void Compositor::handleBatteryStatusMessageFromMce(DBusMessage *signalOrReplyMessage)
{
    const char *batteryStatusName = 0;
    if (parseArgumentsFromDbusMessage(signalOrReplyMessage, DBUS_TYPE_STRING, &batteryStatusName, DBUS_TYPE_INVALID))
        updateInternallyCachedBatteryStatus(batteryStatusName);
}

void Compositor::handleBatteryStatusSignalFromMce(DBusMessage *signalyMessage)
{
    handleBatteryStatusMessageFromMce(signalyMessage);
}

void Compositor::handleBatteryStatusReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        thisAsUserDataPointer->handleBatteryStatusMessageFromMce(replyMessage);
        dbus_message_unref(replyMessage);
    }
}

void Compositor::sendBatteryStatusQueryToMce()
{
    log_debug("battery status: query");
    sendDbusMethodCallMessage(m_systemBusConnection, MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF,
                              MCE_BATTERY_STATUS_GET, handleBatteryStatusReplyFromMce,
                              static_cast<void *>(this), 0, 0, DBUS_TYPE_INVALID);
}

int Compositor::batteryLevel() const
{
    return m_batteryLevel;
}

void Compositor::batteryLevelChanged()
{
    /* This is a stub virtual function */
    log_debug("ignoring: battery level change");
}

void Compositor::updateInsternallyCacheBatteryLevel(int batteryLevel)
{
    if (m_batteryLevel != batteryLevel) {
        m_batteryLevel = batteryLevel;
        log_debug("m_batteryLevel: " << m_batteryLevel);
        batteryLevelChanged();
    }
}

void Compositor::handlebatteryLevelMessageFromMce(DBusMessage *signalOrReplyMessage)
{
    dbus_int32_t batteryLevel = 0;
    if (parseArgumentsFromDbusMessage(signalOrReplyMessage, DBUS_TYPE_INT32, &batteryLevel, DBUS_TYPE_INVALID))
        updateInsternallyCacheBatteryLevel(batteryLevel);
}

void Compositor::handlebatteryLevelSignalFromMce(DBusMessage *signalMessage)
{
    handlebatteryLevelMessageFromMce(signalMessage);
}

void Compositor::handleBatteryLevelReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        thisAsUserDataPointer->handlebatteryLevelMessageFromMce(replyMessage);
        dbus_message_unref(replyMessage);
    }
}

void Compositor::sendBatteryLevelQueryToMce()
{
    log_debug("battery level: query");
    sendDbusMethodCallMessage(m_systemBusConnection, MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF,
                              MCE_BATTERY_LEVEL_GET, handleBatteryLevelReplyFromMce,
                              static_cast<void *>(this), 0, 0, DBUS_TYPE_INVALID);
}

void
Compositor::sendBlankingPauseStopRequestToMce()
{
    if (mceIsRunning())
        sendDbusMethodCallMessage(m_systemBusConnection, MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF,
                                  MCE_CANCEL_PREVENT_BLANK_REQ, 0, 0, 0, 0, DBUS_TYPE_INVALID);
}

void Compositor::sendBlankingPauseStartRequestToMce()
{
    sendDbusMethodCallMessage(m_systemBusConnection, MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF,
                              MCE_PREVENT_BLANK_REQ, 0, 0, 0, 0, DBUS_TYPE_INVALID);
}

void Compositor::evaluateNeedForBlankingPauseTimer()
{
    /* MCE allows 60 second renew delay. Note that there is no named
     * constant for this value. */
    const int renewDelay = 60 * 1000; /* [ms] */
    bool preventBlanking = m_blankPreventWanted && m_blankPreventAllowed;
    if (preventBlanking) {
        if (!m_blankPreventRenewTimer) {
            log_debug("blanking pause: start");
            sendBlankingPauseStartRequestToMce();
            m_blankPreventRenewTimer = m_eventLoop->createTimer(renewDelay,
                                                                [this]() {
                                                                  log_debug("blanking pause: renew");
                                                                  sendBlankingPauseStartRequestToMce();
                                                                });
        }
    } else {
        if (m_blankPreventRenewTimer) {
            log_debug("blanking pause: stop");
            m_eventLoop->cancelTimer(m_blankPreventRenewTimer);
            m_blankPreventRenewTimer = 0;
            sendBlankingPauseStopRequestToMce();
        }
    }
}

void Compositor::setBlankPreventWanted(bool blankPreventWanted)
{
    if (m_blankPreventWanted != blankPreventWanted) {
        m_blankPreventWanted = blankPreventWanted;
        log_debug("m_blankPreventWanted: " << m_blankPreventWanted);
        evaluateNeedForBlankingPauseTimer();
    }
}

void Compositor::updateInternallyCachedBlankPreventAllowed(bool blankPreventAllowed)
{
    if (m_blankPreventAllowed != blankPreventAllowed) {
        m_blankPreventAllowed = blankPreventAllowed;
        log_debug("m_blankPreventAllowed: " << m_blankPreventAllowed);
        evaluateNeedForBlankingPauseTimer();
    }
}

void Compositor::handleBlankPreventAllowedMessageFromMce(DBusMessage *signalOrReplyMessage)
{
    dbus_bool_t blankPreventAllowed = false;
    if (parseArgumentsFromDbusMessage(signalOrReplyMessage, DBUS_TYPE_BOOLEAN, &blankPreventAllowed, DBUS_TYPE_INVALID))
        updateInternallyCachedBlankPreventAllowed(blankPreventAllowed);
}

void Compositor::handleBlankPreventAllowedSignalFromMce(DBusMessage *signalMessage)
{
    handleBlankPreventAllowedMessageFromMce(signalMessage);
}

void Compositor::handleBlankPreventAllowedReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        thisAsUserDataPointer->handleBlankPreventAllowedMessageFromMce(replyMessage);
        dbus_message_unref(replyMessage);
    }
}

void Compositor::sendBlankPreventAllowedQueryToMce()
{
    sendDbusMethodCallMessage(m_systemBusConnection, MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF,
                              MCE_PREVENT_BLANK_ALLOWED_GET, handleBlankPreventAllowedReplyFromMce,
                              static_cast<void *>(this), 0, 0, DBUS_TYPE_INVALID);
}

bool Compositor::mceIsRunning() const
{
    return m_mceIsRunning;
}

void Compositor::updateInternallyCachedMceIsRunning(bool mceIsRunning)
{
    if (m_mceIsRunning != mceIsRunning) {
        m_mceIsRunning = mceIsRunning;
        log_debug("m_mceIsRunning: " << m_mceIsRunning);
        if (m_mceIsRunning) {
            /* Note: Getting updatesEnabled in sync is handled by mce side */
            sendDisplayStateQueryToMce();
            sendChargerStateQueryToMce();
            sendBatteryStatusQueryToMce();
            sendBatteryLevelQueryToMce();
            sendBlankPreventAllowedQueryToMce();
        } else {
            /* Stop blank prevent ping-pong */
            updateInternallyCachedBlankPreventAllowed(false);
        }
    }
}

void Compositor::updateInternallyCachedMceNameOwner(const char *mceNameOwner)
{
    if (mceNameOwner && !*mceNameOwner)
        mceNameOwner = nullptr;
    if (!stringsAreEqual(m_mceNameOwner, mceNameOwner)) {
        log_debug("m_mceNameOwner: "
                  << stringRepr(m_mceNameOwner) << " ->"
                  << stringRepr(mceNameOwner));
        /* If we ever see handoff from one owner to another, it needs
         * to be handled as: oldOwner -> noOwner -> newOwner.
         */
        free(m_mceNameOwner);
        m_mceNameOwner = nullptr;
        updateInternallyCachedMceIsRunning(false);
        if (mceNameOwner) {
            m_mceNameOwner = strdup(mceNameOwner);
            updateInternallyCachedMceIsRunning(true);
        }
    }
}

void Compositor::handleMceNameOwnerSignalFromDbusDaemon(DBusMessage *signalMessage)
{
    const char *dbusName = 0;
    const char *previousNameOwner = 0;
    const char *currentNameOwner = 0;
    if (parseArgumentsFromDbusMessage(signalMessage, DBUS_TYPE_STRING, &dbusName,
                              DBUS_TYPE_STRING, &previousNameOwner,
                              DBUS_TYPE_STRING, &currentNameOwner, DBUS_TYPE_INVALID)) {
        if (stringsAreEqual(dbusName, MCE_SERVICE))
            updateInternallyCachedMceNameOwner(currentNameOwner);
    }
}

void Compositor::handleMceNameOwnerReplyFromDbusDaemon(DBusPendingCall *pendingCall, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        const char *currentNameOwner = 0;
        if (parseArgumentsFromDbusMessage(replyMessage, DBUS_TYPE_STRING, &currentNameOwner, DBUS_TYPE_INVALID))
            thisAsUserDataPointer->updateInternallyCachedMceNameOwner(currentNameOwner);
        dbus_message_unref(replyMessage);
    }
}

void Compositor::sendMceNameOwnerQueryToDbusDaemon()
{
    const char *argument = MCE_SERVICE;
    sendDbusMethodCallMessage(m_systemBusConnection, DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS,
                              "GetNameOwner", handleMceNameOwnerReplyFromDbusDaemon,
                              static_cast<void *>(this), 0, 0, DBUS_TYPE_STRING, &argument, DBUS_TYPE_INVALID);
}

bool Compositor::updatesEnabled() const
{
    return m_updatesState == UpdatesState::UpdatesEnabled;
}

void Compositor::updatesEnabledChanged()
{
    /* This is a stub virtual function */
    log_debug("ignoring: updates enabled change");
}

void Compositor::updateInternallyCacheUpdatesEnabled(bool updatesEnabled)
{
    UpdatesState updatesState = updatesEnabled ? UpdatesState::UpdatesEnabled : UpdatesState::UpdatesDisabled;
    if (m_updatesState != updatesState) {
        m_updatesState = updatesState;
        log_debug("updates state: " << m_updatesState);
        updatesEnabledChanged();
    }
}

DBusMessage *Compositor::handleUpdatesEnabledMethodCallMessage(DBusMessage *methodCallMessage)
{
    /* Note: We always need to react to updatesEnabled changes. If there
     *       any problems, the safest thing we can do is to: Act as if
     *       updates were disabled and always send a reply message to
     *       unblock mce side state machinery.
     */
    dbus_bool_t updatesEnabled = false;
    if (!parseArgumentsFromDbusMessage(methodCallMessage, DBUS_TYPE_BOOLEAN, &updatesEnabled, DBUS_TYPE_INVALID))
        log_err("updates enabled parse error");
    updateInternallyCacheUpdatesEnabled(updatesEnabled);
    return dbus_message_new_method_return(methodCallMessage);
}

DBusMessage *Compositor::handleTopmostWindowPidHandlerMethodCallMessage(DBusMessage *methodCallMessage)
{
    /* Note: Since topmostWindowPid we report never changes, there is
     *       no need to implement COMPOSITOR_SIGNAL_TOPMOST_WINDOW_PID_CHANGED
     *       signal broadcasting.
     */
    DBusMessage *replyMessage = dbus_message_new_method_return(methodCallMessage);
    dbus_int32_t pidAsInt32 = getpid();
    dbus_message_append_args(replyMessage, DBUS_TYPE_INT32, &pidAsInt32, DBUS_TYPE_INVALID);
    return replyMessage;
}

DBusMessage *Compositor::handleGetRequirementsHandlerMethodCallMessage(DBusMessage *methodCallMessage)
{
    /* Be prepared to tell mce that HW compositor service should be stopped before
     * we are given permission to draw via setUpdatesEnabled() method call.
     */
    DBusMessage *replyMessage = dbus_message_new_method_return(methodCallMessage);
    dbus_uint32_t flagsAsUint32 = COMPOSITOR_ACTION_STOP_HWC;
    dbus_message_append_args(replyMessage, DBUS_TYPE_UINT32, &flagsAsUint32, DBUS_TYPE_INVALID);
    return replyMessage;
}

DBusMessage *Compositor::handleIntrospectMethodCallMessage(DBusMessage *methodCallMessage)
{
    static const char *xmlDataString =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.nemomobile.compositor\">\n"
    "    <method name=\"setUpdatesEnabled\">\n"
    "      <arg direction=\"in\" type=\"b\" name=\"enabled\"/>\n"
    "    </method>\n"
    "    <method name=\"privateTopmostWindowProcessId\">\n"
    "      <arg direction=\"out\" type=\"i\" name=\"pid\"/>\n"
    "    </method>\n"
    "    <signal name=\"privateTopmostWindowProcessIdChanged\">\n"
    "      <arg type=\"i\" name=\"pid\"/>\n"
    "    </signal>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "    <method name=\"Introspect\">\n"
    "      <arg name=\"xml_data\" type=\"s\" direction=\"out\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "</node>\n";
    DBusMessage *replyMessage = dbus_message_new_method_return(methodCallMessage);
    dbus_message_append_args(replyMessage, DBUS_TYPE_STRING, &xmlDataString, DBUS_TYPE_INVALID);
    return replyMessage;
}

bool Compositor::compositorNameOwned() const
{
    return m_compositorNameOwned;
}

void Compositor::updateInternallyCachedCompositorOwned(bool compositorNameOwned)
{
    if (m_compositorNameOwned != compositorNameOwned) {
        m_compositorNameOwned = compositorNameOwned;
        log_debug("m_compositorNameOwned: " << m_compositorNameOwned);
        if (m_compositorNameOwned) {
            evaluateCompositorNameOwnerReplacingAllowed();
        } else {
            log_debug("compositor handoff done; exiting");
            m_eventLoop->exit();
        }
    }
}

void Compositor::handleCompositorNameLostSignalFromDbusDaemon(DBusMessage *signalMessage)
{
    const char *dbusName = 0;
    if (parseArgumentsFromDbusMessage(signalMessage, DBUS_TYPE_STRING, &dbusName, DBUS_TYPE_INVALID)) {
        log_debug("name lost: " << dbusName);
        if (stringsAreEqual(dbusName, COMPOSITOR_SERVICE)) {
            updateInternallyCachedCompositorOwned(false);
        }
    }
}

void Compositor::handleCompositorNameAcquiredSignalFromDbusDaemon(DBusMessage *signalMessage)
{
    const char *dbusName = 0;
    if (parseArgumentsFromDbusMessage(signalMessage, DBUS_TYPE_STRING, &dbusName, DBUS_TYPE_INVALID)) {
        log_debug("name acquired: " << dbusName);
        if (stringsAreEqual(dbusName, COMPOSITOR_SERVICE)) {
            updateInternallyCachedCompositorOwned(true);
        }
    }
}

bool Compositor::sendCompositorNameOwnershipRequestToDbusDaemon()
{
    DBusError dbusError = DBUS_ERROR_INIT;
    unsigned flags = DBUS_NAME_FLAG_DO_NOT_QUEUE | DBUS_NAME_FLAG_REPLACE_EXISTING;
    int returnCode = dbus_bus_request_name(m_systemBusConnection, COMPOSITOR_SERVICE, flags, &dbusError);
    if (dbus_error_is_set(&dbusError)) {
        log_err("failed to obtain dbus name:" << dbusError.name << ": " <<  dbusError.message);
    } else if (returnCode != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        log_err("failed to obtain dbus name");
    } else {
        updateInternallyCachedCompositorOwned(true);
        addSystemBusMatches();
        subscribeSystemdNotifications();
        sendMceNameOwnerQueryToDbusDaemon();
        dsmeNameOwnerQuery();
        targetUnitPropsQuery();
    }
    dbus_error_free(&dbusError);
    return compositorNameOwned();
}

void Compositor::evaluateCompositorNameOwnerReplacingAllowed(void)
{
    if (!m_replacingCompositorNameOwnerAllowed && compositorNameOwned() && targetUnitActive()) {
        DBusError dbusError = DBUS_ERROR_INIT;
        unsigned flags = (DBUS_NAME_FLAG_DO_NOT_QUEUE |
                          DBUS_NAME_FLAG_ALLOW_REPLACEMENT);
        int returnCode = dbus_bus_request_name(m_systemBusConnection, COMPOSITOR_SERVICE, flags, &dbusError);
        if (dbus_error_is_set(&dbusError)) {
            log_err("failed to adjust dbus name:" << dbusError.name << ": " <<  dbusError.message);
        } else if (returnCode != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER && returnCode != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER) {
            log_err("failed to adjust dbus name: returnCode=" << returnCode);
        } else {
            log_debug("compositor handoff allowed");
            m_replacingCompositorNameOwnerAllowed = true;
        }
        dbus_error_free(&dbusError);
    }
    if (targetUnitActive() && !m_replacingCompositorNameOwnerAllowed) {
        log_err("compositor handoff not possible; exit immediately");
        m_eventLoop->exit();
    }
}

void Compositor::onBatteryEmpty()
{
    /* Emitted on entry to battery empty state.
     *
     * Device will shut down soon after this unless
     * charger is connected.
     */
    /* This is a stub virtual function */
    log_debug("ignoring: battery empty notification");
}

void Compositor::onThermalShutdown()
{
    /* Emitted on entry to critical thermal state.
     *
     * Device will shut down soon after this unless
     * it gets significantly cooler.
     */
    /* This is a stub virtual function */
    log_debug("ignoring: critical thermal state notification");
}

void Compositor::onSaveUnsavedData()
{
    /* Apps have 2 seconds or so to save state
     * before shutdown commences.
     */
    /* This is a stub virtual function */
    log_debug("ignoring: save data notification");
}

void Compositor::onShutdown()
{
    /* Emitted on entry to shudown/reboot dsme state.
     *
     * Shutdown is imminent.
     */
    /* This is a stub virtual function */
    log_debug("ignoring: shutdown");
}

void Compositor::handleBatteryEmptySignalFromDsme(DBusMessage *signalMessage)
{
    if (parseArgumentsFromDbusMessage(signalMessage, DBUS_TYPE_INVALID))
        onBatteryEmpty();
}

void Compositor::handleSaveUnsavedDataSignalFromDsme(DBusMessage *signalMessage)
{
    if (parseArgumentsFromDbusMessage(signalMessage, DBUS_TYPE_INVALID))
        onSaveUnsavedData();
}

void Compositor::handleShutdownSignalFromDsme(DBusMessage *signalMessage)
{
    if (parseArgumentsFromDbusMessage(signalMessage, DBUS_TYPE_INVALID))
        onShutdown();
}

void Compositor::handleThermalShutdownSignalFromDsme(DBusMessage *signalMessage)
{
    if (parseArgumentsFromDbusMessage(signalMessage, DBUS_TYPE_INVALID))
        onThermalShutdown();
}

void Compositor::sendShutdownRequestToDsme() const
{
    if (getppid() != 1) {
        log_warning("shutdown requested while debugging; exit");
        m_eventLoop->exit();
    } else if (!dsmeIsRunning()) {
        log_warning("shutdown requested while dsme is not running");
    } else {
        log_debug("sending shutdown request to dsme");
        sendDbusMethodCallMessage(m_systemBusConnection, dsme_service, dsme_req_path, dsme_req_interface,
                                  dsme_req_shutdown, 0, 0, 0, 0, DBUS_TYPE_INVALID);
    }
}

void Compositor::sendRebootRequestToDsme() const
{
    if (getppid() != 1) {
        log_warning("reboot requested while debugging; exit");
        m_eventLoop->exit();
    } else if (!dsmeIsRunning()) {
        log_warning("reboot requested while dsme is not running");
    } else {
        log_debug("sending reboot request to dsme");
        sendDbusMethodCallMessage(m_systemBusConnection, dsme_service, dsme_req_path, dsme_req_interface,
                                  dsme_req_reboot, 0, 0, 0, 0, DBUS_TYPE_INVALID);
    }
}

void Compositor::sendPowerupRequestToDsme() const
{
    if (getppid() != 1) {
        log_warning("powerup requested while debugging; exit");
        m_eventLoop->exit();
    } else if (!dsmeIsRunning()) {
        log_warning("powerup requested while dsme is not running");
    } else {
        log_debug("sending powerup request to dsme");
        sendDbusMethodCallMessage(m_systemBusConnection, dsme_service, dsme_req_path, dsme_req_interface,
                                  dsme_req_powerup, 0, 0, 0, 0, DBUS_TYPE_INVALID);
    }
}

bool Compositor::shuttingDownToPowerOff() const
{
    return dsmeState() == DSME_STATE_SHUTDOWN;
}

bool Compositor::shuttingDownToReboot() const
{
    return dsmeState() == DSME_STATE_REBOOT;
}

bool Compositor::shuttingDown() const
{
    return shuttingDownToPowerOff() || shuttingDownToReboot();
}

Compositor::DsmeState Compositor::dsmeState() const
{
    return m_dsmeState;
}

void Compositor::dsmeStateChanged()
{
    /* This is a stub virtual function */
    log_debug("ignoring: dsme state change");
}

void Compositor::updateInternallyCacheDsmeState(DsmeState dsmeState)
{
    if (m_dsmeState != dsmeState) {
        log_debug("m_dsmeState: "
                  << lookupDsmeStateName(m_dsmeState)
                  << " -> "
                  << lookupDsmeStateName(dsmeState));
        m_dsmeState = dsmeState;
        dsmeStateChanged();
    }
}

void Compositor::handleDsmeStateMessageFromDsme(DBusMessage *signalOrReplyMessage)
{
    const char *stateName = 0;
    if (parseArgumentsFromDbusMessage(signalOrReplyMessage, DBUS_TYPE_STRING, &stateName, DBUS_TYPE_INVALID))
        updateInternallyCacheDsmeState(lookupDsmeStateValue(stateName));
}

void Compositor::handleDsmeStateSignalFromDsme(DBusMessage *signalMessage)
{
    handleDsmeStateMessageFromDsme(signalMessage);
}

void Compositor::handleDsmeStateReplyFromDsme(DBusPendingCall *pendingCall, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        thisAsUserDataPointer->handleDsmeStateMessageFromDsme(replyMessage);
        dbus_message_unref(replyMessage);
    }
}

void Compositor::sendDsmeStateQuerytoDsme()
{
    sendDbusMethodCallMessage(m_systemBusConnection, dsme_service, dsme_req_path, dsme_req_interface,
                              dsme_get_state, handleDsmeStateReplyFromDsme,
                              static_cast<void *>(this), 0, 0, DBUS_TYPE_INVALID);
}

bool Compositor::dsmeIsRunning() const
{
    return m_dsmeIsRunning;
}

void Compositor::updateInternallyCachedDsmeIsRunning(bool dsmeIsRunning)
{
    if (m_dsmeIsRunning != dsmeIsRunning) {
        m_dsmeIsRunning = dsmeIsRunning;
        log_debug("m_dsmeIsRunning: " << m_dsmeIsRunning);
        if (m_dsmeIsRunning) {
            sendDsmeStateQuerytoDsme();
        } else {
            updateInternallyCacheDsmeState(DSME_STATE_NOT_SET);
        }
    }
}

void Compositor::updateInternallyCachedDsmeNameOwner(const char *dsmeNameOwner)
{
    if (dsmeNameOwner && !*dsmeNameOwner)
        dsmeNameOwner = nullptr;
    if (!stringsAreEqual(m_dsmeNameOwner, dsmeNameOwner)) {
        log_debug("m_dsmeNameOwner: "
                  << stringRepr(m_dsmeNameOwner) << " ->"
                  << stringRepr(dsmeNameOwner));
        /* If we ever see handoff from one owner to another, it needs
         * to be handled as: oldOwner -> noOwner -> newOwner.
         */
        free(m_dsmeNameOwner);
        m_dsmeNameOwner = nullptr;
        updateInternallyCachedDsmeIsRunning(false);
        if (dsmeNameOwner) {
            m_dsmeNameOwner = strdup(dsmeNameOwner);
            updateInternallyCachedDsmeIsRunning(true);
        }
    }
}

void Compositor::dsmeNameOwnerHandler(DBusMessage *signalMessage)
{
    const char *dbusName = 0;
    const char *previousNameOwner = 0;
    const char *currentNameOwner = 0;
    if (parseArgumentsFromDbusMessage(signalMessage, DBUS_TYPE_STRING, &dbusName, DBUS_TYPE_STRING, &previousNameOwner,
                              DBUS_TYPE_STRING, &currentNameOwner, DBUS_TYPE_INVALID)) {
        if (stringsAreEqual(dbusName, dsme_service))
            updateInternallyCachedDsmeNameOwner(currentNameOwner);
    }
}

void Compositor::dsmeNameOwnerReply(DBusPendingCall *pendingCall, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        const char *currentNameOwner = 0;
        if (parseArgumentsFromDbusMessage(replyMessage, DBUS_TYPE_STRING, &currentNameOwner, DBUS_TYPE_INVALID))
            thisAsUserDataPointer->updateInternallyCachedDsmeNameOwner(currentNameOwner);
        dbus_message_unref(replyMessage);
    }
}

void Compositor::dsmeNameOwnerQuery()
{
    const char *dbusName = dsme_service;
    sendDbusMethodCallMessage(m_systemBusConnection, DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                              DBUS_INTERFACE_DBUS, "GetNameOwner", dsmeNameOwnerReply,
                              static_cast<void *>(this), 0, 0, DBUS_TYPE_STRING, &dbusName,
                              DBUS_TYPE_INVALID);
}

bool Compositor::targetUnitActive() const
{
    return m_targetUnitActive;
}

void Compositor::targetUnitActiveChanged()
{
    /* This is a stub virtual function */
    log_debug("ignoring: target unit active change");
}

void Compositor::updateTargetUnitActive(bool targetUnitActive)
{
    if (m_targetUnitActive != targetUnitActive) {
        m_targetUnitActive = targetUnitActive;
        log_debug("m_targetUnitActive: " << m_targetUnitActive);
        targetUnitActiveChanged();
        evaluateCompositorNameOwnerReplacingAllowed();
    }
}

void Compositor::evaluateTargetUnitActive()
{
    bool targetUnitActive = false;
    const char *activeState = m_targetUnitProperties->activeState();
    if (stringsAreEqual(activeState, "active")) {
        const char *subState = m_targetUnitProperties->subState();
        targetUnitActive = stringsAreEqual(subState, "running") || stringsAreEqual(subState, "mounted");
    }
    updateTargetUnitActive(targetUnitActive);
}

void Compositor::targetUnitPropsHandler(DBusMessage *signalOrReplyMessage)
{
    const char *interfaceName = 0;
    DBusMessageIter messageIter;
    if (dbus_message_iter_init(signalOrReplyMessage, &messageIter)) {
        if (parseStringValueFromMessageIter(&messageIter, &interfaceName)) {
            log_debug("property change: " << interfaceName);
            if (stringsAreEqual(interfaceName, SYSTEMD_UNIT_INTERFACE)) {
                m_targetUnitProperties->updateFromMessageIter(&messageIter);
                evaluateTargetUnitActive();
            }
        }
    }
}

void Compositor::targetUnitPropsReply(DBusPendingCall *pendingCall, void *userDataPointer)
{
    log_debug("property reply");
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    DBusMessage *replyMessage = 0;
    if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
        DBusMessageIter messageIter;
        if (dbus_message_iter_init(replyMessage, &messageIter)) {
            thisAsUserDataPointer->m_targetUnitProperties->updateFromMessageIter(&messageIter);
            thisAsUserDataPointer->evaluateTargetUnitActive();
        }
        dbus_message_unref(replyMessage);
    }
}

void Compositor::targetUnitPropsQuery()
{
    log_debug("property query");
    const char *interfaceName = SYSTEMD_UNIT_INTERFACE;
    sendDbusMethodCallMessage(m_systemBusConnection, SYSTEMD_SERVICE, SYSTEMD_TARGET_UNIT_PATH,
                              DBUS_PROPERTIES_INTERFACE, DBUS_PROPERTIES_METHOD_GET_ALL,
                              targetUnitPropsReply, static_cast<void *>(this), 0, 0,
                              DBUS_TYPE_STRING, &interfaceName, DBUS_TYPE_INVALID);
}

void Compositor::subscribeSystemdNotifications() const
{
    log_debug("subscribe: call");
    sendDbusMethodCallMessage(m_systemBusConnection,
                              SYSTEMD_SERVICE, SYSTEMD_MANAGER_PATH, SYSTEMD_MANAGER_INTERFACE,
                              SYSTEMD_MANAGER_METHOD_SUBSCRIBE,
                              [](DBusPendingCall *pendingCall, void *userDataPointer) {
                                  (void)userDataPointer;
                                  DBusMessage *replyMessage = 0;
                                  if ((replyMessage = getReplyMessageFromPendingCall(pendingCall))) {
                                      log_debug("subscribe: reply");
                                      dbus_message_unref(replyMessage);
                                  }
                              }, 0, 0, 0, DBUS_TYPE_INVALID);
}

Compositor::SignalMessageHandler Compositor::s_systemBusSignalHandlers[] =
{
    {
        &Compositor::handleMceNameOwnerSignalFromDbusDaemon,
        DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "NameOwnerChanged",
        MCE_SERVICE, nullptr, nullptr
    },
    {
        &Compositor::handleCompositorNameLostSignalFromDbusDaemon,
        DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "NameLost",
        COMPOSITOR_SERVICE, nullptr, nullptr
    },
    {
        &Compositor::handleCompositorNameAcquiredSignalFromDbusDaemon,
        DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "NameAcquired",
        COMPOSITOR_SERVICE, nullptr, nullptr
    },
    {
        &Compositor::handleDisplayStateSignalFromMce,
        MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_DISPLAY_SIG,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::handleChargerStateSignalFromMce,
        MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_CHARGER_STATE_SIG,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::handleBatteryStatusSignalFromMce,
        MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_BATTERY_STATUS_SIG,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::handlebatteryLevelSignalFromMce,
        MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_BATTERY_LEVEL_SIG,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::handleBlankPreventAllowedSignalFromMce,
        MCE_SIGNAL_PATH, MCE_SIGNAL_IF, MCE_PREVENT_BLANK_ALLOWED_SIG,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::targetUnitPropsHandler,
        SYSTEMD_TARGET_UNIT_PATH, DBUS_PROPERTIES_INTERFACE, DBUS_PROPERTIES_SIGNAL_CHANGED,
        SYSTEMD_UNIT_INTERFACE, nullptr, nullptr
    },
    {
        &Compositor::dsmeNameOwnerHandler,
        DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "NameOwnerChanged",
        dsme_service, nullptr, nullptr
    },
    {
        &Compositor::handleBatteryEmptySignalFromDsme,
        dsme_sig_path, dsme_sig_interface, dsme_battery_empty_ind,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::handleSaveUnsavedDataSignalFromDsme,
        dsme_sig_path, dsme_sig_interface, dsme_save_unsaved_data_ind,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::handleShutdownSignalFromDsme,
        dsme_sig_path, dsme_sig_interface, dsme_shutdown_ind,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::handleThermalShutdownSignalFromDsme,
        dsme_sig_path, dsme_sig_interface, dsme_thermal_shutdown_ind,
        nullptr, nullptr, nullptr
    },
    {
        &Compositor::handleDsmeStateSignalFromDsme,
        dsme_sig_path, dsme_sig_interface, dsme_state_change_ind,
        nullptr, nullptr, nullptr
    },
    /* Sentinel */
    {
        nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr
    }
};

Compositor::MethodCallMessageHandler Compositor::s_systemBusMethodCallHandlers[] = {
    {
        &Compositor::handleUpdatesEnabledMethodCallMessage,
        COMPOSITOR_SERVICE, COMPOSITOR_PATH, COMPOSITOR_INTERFACE, COMPOSITOR_METHOD_SET_UPDATES_ENABLED
    },
    {
        &Compositor::handleTopmostWindowPidHandlerMethodCallMessage,
        COMPOSITOR_SERVICE, COMPOSITOR_PATH, COMPOSITOR_INTERFACE, COMPOSITOR_METHOD_GET_TOPMOST_WINDOW_PID
    },
    {
        &Compositor::handleGetRequirementsHandlerMethodCallMessage,
        COMPOSITOR_SERVICE, COMPOSITOR_PATH, COMPOSITOR_INTERFACE, COMPOSITOR_METHOD_GET_SETUP_ACTIONS
    },
    {
        &Compositor::handleIntrospectMethodCallMessage,
        COMPOSITOR_SERVICE, COMPOSITOR_PATH, "org.freedesktop.DBus.Introspectable", "Introspect"
    },
    /* Sentinel */
    {
        nullptr,
        nullptr, nullptr, nullptr, nullptr
    }
};

DBusHandlerResult Compositor::systemBusMessageFilter(DBusConnection *dbusConnection, DBusMessage *dbusMessage, void *userDataPointer)
{
    Compositor *thisAsUserDataPointer = static_cast<Compositor*>(userDataPointer);
    bool messageWasHandled = false;
    int msgType = dbus_message_get_type(dbusMessage);
    if (msgType == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        const char *serviceName = dbus_message_get_destination(dbusMessage);
        const char *objectPath = dbus_message_get_path(dbusMessage);
        const char *interfaceName = dbus_message_get_interface(dbusMessage);
        const char *memberName = dbus_message_get_member(dbusMessage);
        if (serviceName && objectPath && interfaceName && memberName) {
            for (size_t i = 0; s_systemBusMethodCallHandlers[i].m_methodCallMessageHanderFunction; ++i) {
                if (!stringsAreEqual(s_systemBusMethodCallHandlers[i].m_serviceName, serviceName)
                    || !stringsAreEqualOrWeDoNotCare(s_systemBusMethodCallHandlers[i].m_interfaceName, interfaceName)
                    || !stringsAreEqualOrWeDoNotCare(s_systemBusMethodCallHandlers[i].m_objectPath, objectPath)
                    || !stringsAreEqualOrWeDoNotCare(s_systemBusMethodCallHandlers[i].m_methodName, memberName))
                    continue;
                log_debug("handle method call: " << interfaceName << "." << memberName << "()");
                DBusMessage *replyMessage = (thisAsUserDataPointer->*s_systemBusMethodCallHandlers[i].m_methodCallMessageHanderFunction)(dbusMessage);
                if (!dbus_message_get_no_reply(dbusMessage)) {
                    if (!replyMessage)
                        replyMessage = dbus_message_new_error(dbusMessage, DBUS_ERROR_FAILED, memberName);
                    if (!replyMessage || !dbus_connection_send(dbusConnection, replyMessage, 0))
                        log_err("failed to send reply");
                }
                if (replyMessage)
                    dbus_message_unref(replyMessage);
                messageWasHandled = true;
                break;
            }
        }
    } else if (msgType == DBUS_MESSAGE_TYPE_SIGNAL) {
        const char *objectPath = dbus_message_get_path(dbusMessage);
        const char *interfaceName = dbus_message_get_interface(dbusMessage);
        const char *memberName = dbus_message_get_member(dbusMessage);
        if (objectPath && interfaceName && memberName) {
            const char *argument0 = nullptr;
            const char *argument1 = nullptr;
            const char *argument2 = nullptr;
            DBusMessageIter messageIter;
            dbus_message_iter_init(dbusMessage, &messageIter);
            parseStringArgumentFromMessageIter(&messageIter, &argument0);
            parseStringArgumentFromMessageIter(&messageIter, &argument1);
            parseStringArgumentFromMessageIter(&messageIter, &argument2);
            for (size_t i = 0; s_systemBusSignalHandlers[i].m_signalMessageHanderFunction; ++i) {
                if (!stringsAreEqualOrWeDoNotCare(s_systemBusSignalHandlers[i].m_interfaceName, interfaceName)
                    || !stringsAreEqualOrWeDoNotCare(s_systemBusSignalHandlers[i].m_objectPath, objectPath)
                    || !stringsAreEqualOrWeDoNotCare(s_systemBusSignalHandlers[i].m_signalName, memberName)
                    || !stringsAreEqualOrWeDoNotCare(s_systemBusSignalHandlers[i].m_argument0, argument0)
                    || !stringsAreEqualOrWeDoNotCare(s_systemBusSignalHandlers[i].m_argument1, argument1)
                    || !stringsAreEqualOrWeDoNotCare(s_systemBusSignalHandlers[i].m_argument2, argument2))
                    continue;
                log_debug("handle signal: " << interfaceName << "." << memberName << "()");
                (thisAsUserDataPointer->*s_systemBusSignalHandlers[i].m_signalMessageHanderFunction)(dbusMessage);
            }
        }
    }
    return messageWasHandled ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

bool Compositor::generateMatchForSignalMessageHandler(Compositor::SignalMessageHandler &signalMessageHandler, char *matchBuffer, size_t matchBufferSize)
{
    char interfaceName[64] = "";
    char objectPath[64] = "";
    char memberName[64] = "";
    char argument0[64] = "";
    char argument1[64] = "";
    char argument2[64] = "";
    /* Skip implicitly sent signals such as NameAcquired/NameLost */
    if (stringsAreEqual(signalMessageHandler.m_signalName,"NameLost") || stringsAreEqual(signalMessageHandler.m_signalName,"NameAcquired"))
        return false;
    if (signalMessageHandler.m_interfaceName)
        snprintf(interfaceName, sizeof interfaceName, ",interface='%s'", signalMessageHandler.m_interfaceName);
    if (signalMessageHandler.m_objectPath)
        snprintf(objectPath, sizeof objectPath, ",path='%s'", signalMessageHandler.m_objectPath);
    if (signalMessageHandler.m_signalName)
        snprintf(memberName, sizeof memberName, ",member='%s'", signalMessageHandler.m_signalName);
    if (signalMessageHandler.m_argument0)
        snprintf(argument0, sizeof argument0, ",arg0='%s'", signalMessageHandler.m_argument0);
    if (signalMessageHandler.m_argument1)
        snprintf(argument1, sizeof argument1, ",arg1='%s'", signalMessageHandler.m_argument1);
    if (signalMessageHandler.m_argument2)
        snprintf(argument2, sizeof argument2, ",arg2='%s'", signalMessageHandler.m_argument2);
    snprintf(matchBuffer, matchBufferSize, "type=signal%s%s%s%s%s%s", interfaceName, objectPath, memberName, argument0, argument1, argument2);
    return true;
}

void Compositor::addSystemBusMatches()
{
    for (size_t i = 0; s_systemBusSignalHandlers[i].m_signalMessageHanderFunction; ++i) {
        char signalMatch[256];
        if (generateMatchForSignalMessageHandler(s_systemBusSignalHandlers[i], signalMatch, sizeof signalMatch)) {
            log_debug("add match: " << signalMatch);
            dbus_bus_add_match(m_systemBusConnection, signalMatch, 0);
        }
    }
}

void Compositor::removeSystemBusMatches()
{
    for (size_t i = 0; s_systemBusSignalHandlers[i].m_signalMessageHanderFunction; ++i) {
        char signalMatch[256];
        if (generateMatchForSignalMessageHandler(s_systemBusSignalHandlers[i], signalMatch, sizeof signalMatch)) {
            log_debug("remove match: " << signalMatch);
            dbus_bus_remove_match(m_systemBusConnection, signalMatch, 0);
        }
    }
}

bool Compositor::connectToSystemBus()
{
    if (!m_systemBusConnection) {
        log_debug("dbus connect");
        DBusError dbusError = DBUS_ERROR_INIT;
        if (!(m_systemBusConnection = dbus_bus_get(DBUS_BUS_SYSTEM, &dbusError))) {
            log_err("system bus connect failed: "
                    << dbusError.name << ": " <<  dbusError.message);
        } else {
            dbus_connection_add_filter(m_systemBusConnection, systemBusMessageFilter, static_cast<void*>(this), 0);
        }
        dbus_error_free(&dbusError);
    }
    return m_systemBusConnection != 0;
}

void Compositor::disconnectFromSystemBus()
{
    if (m_systemBusConnection) {
        log_debug("dbus disconnect");
        dbus_connection_remove_filter(m_systemBusConnection, systemBusMessageFilter, static_cast<void*>(this));
        if (dbus_connection_get_is_connected(m_systemBusConnection)) {
            removeSystemBusMatches();
            /* Note: Name is left to be released implicitly
             *       when we get disconnected at exit, or
             *       we are exiting because it was already
             *       taken from us via ownership replacement.
             */
        }
        dbus_connection_unref(m_systemBusConnection);
        m_systemBusConnection = nullptr;
    }
}
};
