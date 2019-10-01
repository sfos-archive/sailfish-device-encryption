/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#ifndef UNLOCK_AGENT_PIN_H_
#define UNLOCK_AGENT_PIN_H_
#include <sailfish-minui/ui.h>
#include <sailfish-minui-dbus/eventloop.h>
#include <sailfish-mindbus/object.h>

#include "call.h"
#include "emergencybutton.h"

namespace Sailfish {

class Agent;

class PinUi : public MinUi::Window
{
public:
    PinUi(Agent *agent, MinUi::DBus::EventLoop *eventLoop);
    virtual ~PinUi();

    /**
     * Handle new ask files
     */
    void newAskFile();

    void showError();

    enum {
#if 1
        /* How long to wait for further button presses before
         * shutting down instead of waiting for use to complete
         * the unlock code entry.
         */
        inactivityShutdownDelay = 5 * 60 * 1000, // [ms] -> 5 minutes

        /* How long to wait between meeting battery-is-empty condition
         * and initiating shutdown.
         */
        batteryEmptyShutdownDelay =     10 * 1000, // [ms] -> 10 seconds
#else
        // Shorter valus for test/debug purposes ...
        inactivityShutdownDelay = 30 * 1000, // [ms] -> 30 seconds
        batteryEmptyShutdownDelay =  3 * 1000, // [ms] -> 3 seconds
#endif
    };

private:
    MinUi::Label *createLabel(const char *name, int y);
    void setEmergencyCallStatus(Call::Status status);
    void createTimer(int interval, const std::function<void()> &callback);
    void cancelTimer();

public:
    /**
     * Reset the UI
     */
    void reset();

    void displayStateChanged();
    void chargerStateChanged();
    void batteryStatusChanged();
    void batteryLevelChanged();
    void updatesEnabledChanged();
    void dsmeStateChanged();
    void targetUnitActiveChanged();

private:
    void disableAll();
    void enabledAll();

    void updateAcceptVisibility();
    void createUI();

    bool emergencyMode() const;
    void setEmergencyMode(bool emergency);
    void emergencyModeChanged();

    bool inactivityShutdownEnabled(void) const;
    void setInactivityShutdownEnabled(bool enabled);
    void stopInactivityShutdownTimer();
    void startInactivityShutdownTimer();
    void restartInactivityShutdownTimer();
    void onInactivityShutdownTimer();

    void considerBatteryEmptyShutdown();
    void cancelBatteryEmptyShutdown();
    void onBatteryEmptyShutdown();

private:
    Agent *m_agent;

    MinUi::PasswordField *m_password;
    MinUi::Keypad *m_key;
    MinUi::Label *m_label;
    MinUi::Label *m_warningLabel;
    MinUi::BusyIndicator *m_busyIndicator;

    // Placeholder strings for error handling

    //% "Entering an incorrect security code once more will permanently block your access to the device."
    const char *m_last_chance { qtTrId("sailfish-device-encryption-unlock-ui-la-last_chance") };

    //: Shown when user has chosen emergency call mode
    //% "Emergency call"
    const char *m_emergency_call { qtTrId("sailfish-device-encryption-unlock-ui-la-emergency_call") };

    //: Indicates that user has entered invalid emergency number
    //% "Only emergency calls permitted"
    const char *m_invalid_emergency_number { qtTrId("sailfish-device-encryption-unlock-ui-la-invalid_emergency_number") };

    //: Starts the phone call
    //% "Call"
    const char *m_start_call { qtTrId("sailfish-device-encryption-unlock-ui-bt-start_call") };

    //: Ends the phone call
    //% "End"
    const char *m_end_call { qtTrId("sailfish-device-encryption-unlock-ui-bt-end_call") };

    //: Shown when calling emergency number
    //% "Calling emergency number"
    const char *m_calling_emergency { qtTrId("sailfish-device-encryption-unlock-ui-la-calling_emergency_number") };

    //: Shown when calling emergency number fails
    //% "Emergency call failed"
    const char *m_emergency_call_failed { qtTrId("sailfish-device-encryption-unlock-ui-la-emergency_call_failed") };

    //: Shown when emergency call ends
    //% "Emergency call ended"
    const char *m_emergency_call_ended { qtTrId("sailfish-device-encryption-unlock-ui-la-emergency_call_ended") };

    MinUi::Palette m_palette;
    int m_timer;
    bool m_canShowError;
    bool m_displayOn;
    MinDBus::Object *m_dbus;
    EmergencyButton *m_emergencyButton;
    bool m_emergencyMode;
    MinUi::Rectangle *m_emergencyBackground;
    MinUi::Label *m_emergencyLabel;
    Call m_call;
    MinUi::IconButton *m_speakerButton;
    bool m_inactivityShutdownEnabled;
    int m_inactivityShutdownTimer;
    bool m_batteryEmptyShutdownRequested;
    int m_batteryEmptyShutdownTimer;
    MinUi::Label *m_exitNotification;
};
}
#endif /* UNLOCK_AGENT_PIN_H_ */
