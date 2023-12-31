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
    void destroyWarningLabel();

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

    void handleKeyPress(int code, char character);
    void updateInputItemVisibilities();

private:
    Agent *m_agent;

    MinUi::PasswordField *m_password;
    MinUi::Keyboard *m_keyboard;
    MinUi::Keypad *m_keypad;
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
    MinUi::IconButton *m_keypadButton;
    MinUi::IconButton *m_keyboardButton;
    bool m_inactivityShutdownEnabled;
    int m_inactivityShutdownTimer;
    bool m_batteryEmptyShutdownRequested;
    int m_batteryEmptyShutdownTimer;
    bool m_keypadInUse;
    MinUi::Label *m_exitNotification;
};
}
#endif /* UNLOCK_AGENT_PIN_H_ */
