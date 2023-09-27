/****************************************************************************************
** Copyright (c) 2019 - 2023 Jolla Ltd.
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

#ifndef UNLOCK_AGENT_COMPOSITOR_H_
#define UNLOCK_AGENT_COMPOSITOR_H_
#include <dbus/dbus.h>
#include <sailfish-minui/ui.h>

namespace Sailfish {
class TargetUnitProperties;
class Compositor
{
public:
    enum DsmeState {
#define DSME_STATE(STATE, VALUE) DSME_STATE_##STATE = VALUE,
#include <dsme/state_states.h>
#undef DSME_STATE
    };
    enum DisplayState {
        DisplayUnknown = -1,
        DisplayOff,
        DisplayDim,
        DisplayOn
    };
    enum ChargerState {
        ChargerUnknown = -1,
        ChargerOff,
        ChargerOn
    };
    enum BatteryStatus {
        BatteryUnknown = -1,
        BatteryFull,
        BatteryOk,
        BatteryLow,
        BatteryEmpty
    };
    enum UpdatesState {
      UpdatesUnknown = -1,
      UpdatesDisabled,
      UpdatesEnabled
    };
    Compositor(MinUi::EventLoop *eventLoop);
    virtual ~Compositor();
    DisplayState displayState() const;
    ChargerState chargerState() const;
    BatteryStatus batteryStatus() const;
    int batteryLevel() const;
    bool updatesEnabled() const;
    bool shuttingDownToPowerOff() const;
    bool shuttingDownToReboot() const;
    bool shuttingDown() const;
    DsmeState dsmeState() const;
    bool targetUnitActive() const;
    void setBlankPreventWanted(bool blankPreventWanted);
    void sendShutdownRequestToDsme() const;
    void sendRebootRequestToDsme() const;
    void sendPowerupRequestToDsme() const;
protected:
    virtual void onShutdown();
    virtual void onBatteryEmpty();
    virtual void onThermalShutdown();
    virtual void onSaveUnsavedData();
    virtual void dsmeStateChanged();
    virtual void displayStateChanged();
    virtual void chargerStateChanged();
    virtual void batteryStatusChanged();
    virtual void batteryLevelChanged();
    virtual void updatesEnabledChanged();
    virtual void targetUnitActiveChanged();
private:
    typedef DBusMessage *(Compositor::*MethodCallMessageHandlerFunction)(DBusMessage *methodCallMessage);
    struct MethodCallMessageHandler
    {
        Compositor::MethodCallMessageHandlerFunction m_methodCallMessageHanderFunction;
        const char *m_serviceName;
        const char *m_objectPath;
        const char *m_interfaceName;
        const char *m_methodName;
    };
    typedef void (Compositor::*SignalMessageHandlerFunction)(DBusMessage *signalMessage);
    struct SignalMessageHandler
    {
        Compositor::SignalMessageHandlerFunction m_signalMessageHanderFunction;
        const char *m_objectPath;
        const char *m_interfaceName;
        const char *m_signalName;
        const char *m_argument0;
        const char *m_argument1;
        const char *m_argument2;
    };
    void updateInternallyCachedDisplayState(const char *displayStateName);
    void handleDisplayStateMessageFromMce(DBusMessage *signalMessage);
    void handleDisplayStateSignalFromMce(DBusMessage *signalMessage);
    static void handleDisplayStateReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer);
    void sendDisplayStateQueryToMce();
    void updateInternallyCachedChargerState(const char *chargerStateName);
    void handleChargerStateMessageFromMce(DBusMessage *signalOrReplyMessage);
    void handleChargerStateSignalFromMce(DBusMessage *signalMessage);
    static void handleChargerStateReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer);
    void sendChargerStateQueryToMce();
    void updateInternallyCachedBatteryStatus(const char *batteryStatusName);
    void handleBatteryStatusMessageFromMce(DBusMessage *signalOrReplyMessage);
    void handleBatteryStatusSignalFromMce(DBusMessage *signalMessage);
    static void handleBatteryStatusReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer);
    void sendBatteryStatusQueryToMce();
    void updateInsternallyCacheBatteryLevel(int batteryLevel);
    void handlebatteryLevelMessageFromMce(DBusMessage *signalOrReplyMessage);
    void handlebatteryLevelSignalFromMce(DBusMessage *signalMessage);
    static void handleBatteryLevelReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer);
    void sendBatteryLevelQueryToMce();
    void sendBlankingPauseStopRequestToMce();
    void sendBlankingPauseStartRequestToMce();
    void evaluateNeedForBlankingPauseTimer();
    void updateInternallyCachedBlankPreventAllowed(bool blankPreventAllowed);
    void handleBlankPreventAllowedMessageFromMce(DBusMessage *signalOrReplyMessage);
    void handleBlankPreventAllowedSignalFromMce(DBusMessage *signalMessage);
    static void handleBlankPreventAllowedReplyFromMce(DBusPendingCall *pendingCall, void *userDataPointer);
    void sendBlankPreventAllowedQueryToMce();
    bool mceIsRunning() const;
    void updateInternallyCachedMceIsRunning(bool mceIsRunning);
    void updateInternallyCachedMceNameOwner(const char *mceNameOwner);
    void handleMceNameOwnerSignalFromDbusDaemon(DBusMessage *signalMessage);
    static void handleMceNameOwnerReplyFromDbusDaemon(DBusPendingCall *pendingCall, void *userDataPointer);
    void sendMceNameOwnerQueryToDbusDaemon();
    void updateInternallyCacheUpdatesEnabled(bool updatesEnabled);
    DBusMessage *handleUpdatesEnabledMethodCallMessage(DBusMessage *methodCallMessage);
    DBusMessage *handleTopmostWindowPidHandlerMethodCallMessage(DBusMessage *methodCallMessage);
    DBusMessage *handleGetRequirementsHandlerMethodCallMessage(DBusMessage *methodCallMessage);
    DBusMessage *handleIntrospectMethodCallMessage(DBusMessage *methodCallMessage);
    bool compositorNameOwned() const;
    void updateInternallyCachedCompositorOwned(bool compositorNameOwned);
    void handleCompositorNameLostSignalFromDbusDaemon(DBusMessage *signalMessage);
    void handleCompositorNameAcquiredSignalFromDbusDaemon(DBusMessage *signalMessage);
    bool sendCompositorNameOwnershipRequestToDbusDaemon();
    void evaluateCompositorNameOwnerReplacingAllowed();
    void handleBatteryEmptySignalFromDsme(DBusMessage *signalMessage);
    void handleSaveUnsavedDataSignalFromDsme(DBusMessage *signalMessage);
    void handleShutdownSignalFromDsme(DBusMessage *signalMessage);
    void handleThermalShutdownSignalFromDsme(DBusMessage *signalMessage);
    void updateInternallyCacheDsmeState(DsmeState dsmeState);
    void handleDsmeStateMessageFromDsme(DBusMessage *signalOrReplyMessage);
    void handleDsmeStateSignalFromDsme(DBusMessage *signalMessage);
    static void handleDsmeStateReplyFromDsme(DBusPendingCall *pendingCall, void *userDataPointer);
    void sendDsmeStateQuerytoDsme();
    bool dsmeIsRunning() const;
    void updateInternallyCachedDsmeIsRunning(bool dsmeIsRunning);
    void updateInternallyCachedDsmeNameOwner(const char *dsmeNameOwner);
    void dsmeNameOwnerHandler(DBusMessage *signalMessage);
    static void dsmeNameOwnerReply(DBusPendingCall *pendingCall, void *userDataPointer);
    void dsmeNameOwnerQuery();
    void updateTargetUnitActive(bool targetUnitActive);
    void evaluateTargetUnitActive();
    void targetUnitPropsHandler(DBusMessage *signalOrReplyMessage);
    static void targetUnitPropsReply(DBusPendingCall *pendingCall, void *userDataPointer);
    void targetUnitPropsQuery();
    void subscribeSystemdNotifications() const;
    static DBusHandlerResult systemBusMessageFilter(DBusConnection *dbusConnection, DBusMessage *dbusMessage, void *userDataPointer);
    bool generateMatchForSignalMessageHandler(Compositor::SignalMessageHandler &signalMessageHandler, char *matchBuffer, size_t matchBufferSize);
    void addSystemBusMatches();
    void removeSystemBusMatches();
    bool connectToSystemBus();
    void disconnectFromSystemBus();
    MinUi::EventLoop *m_eventLoop;
    DBusConnection *m_systemBusConnection;
    bool m_targetUnitActive;
    TargetUnitProperties *m_targetUnitProperties;
    char *m_dsmeNameOwner;
    bool m_dsmeIsRunning;
    DsmeState m_dsmeState;
    bool m_compositorNameOwned;
    bool m_replacingCompositorNameOwnerAllowed;
    UpdatesState m_updatesState;
    char *m_mceNameOwner;
    bool m_mceIsRunning;
    bool m_blankPreventWanted;
    bool m_blankPreventAllowed;
    int m_blankPreventRenewTimer;
    int m_batteryLevel;
    BatteryStatus m_batteryStatus;
    ChargerState m_chargerState;
    DisplayState m_displayState;
    static SignalMessageHandler s_systemBusSignalHandlers[];
    static MethodCallMessageHandler s_systemBusMethodCallHandlers[];
};
}
#endif /* UNLOCK_AGENT_COMPOSITOR_H_ */
