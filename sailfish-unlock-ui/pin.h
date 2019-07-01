/*
 * Copyright (C) 2019 Jolla Ltd
 */

#ifndef UNLOCK_AGENT_PIN_H_
#define UNLOCK_AGENT_PIN_H_
#include <sailfish-minui/ui.h>
#include <sailfish-minui/eventloop.h>
#include <sailfish-mindbus/object.h>

#include "call.h"
#include "compositor.h"

namespace Sailfish {

static const char *ask_dir = "/run/systemd/ask-password/";

class PinUi : public MinUi::Window, public Compositor
{
public:
    /**
     * Singleton constructor
     */
    static PinUi* instance();

    /**
     * Start the eventloop
     * @param socket Socket where to send the password
     * @return Eventloop exit code
     */
    int execute(const char* socket);

    /**
     * Exit the eventloop
     * @param value Exit value
     */
    void exit(int value);

    void showError();

private:
    PinUi(MinUi::DBus::EventLoop *eventLoop);
    virtual ~PinUi();
    MinUi::Label *createLabel(const char *name, int y);
    void setEmergencyCallStatus(Call::Status status);

public:
    /**
     * Reset the UI
     */
    void reset();

private:
    void disableAll();
    void enabledAll();

    void updateAcceptVisibility();
    void createUI();
    virtual void displayStateChanged();
    virtual void updatesEnabledChanged();
    void sendPassword(const std::string& password);
    void startAskWatcher();
    static bool askWatcher(int descriptor, uint32_t events);
    void setEmergencyMode(bool emergency);

private:
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
    bool m_createdUI;
    bool m_displayOn;
    bool m_checkTemporaryKey;
    static PinUi *s_instance;
    MinDBus::Object *m_dbus;
    const char *m_socket;
    bool m_watcher;
    MinUi::IconButton *m_emergencyButton;
    bool m_emergencyMode;
    MinUi::Rectangle *m_emergencyBackground;
    MinUi::Label *m_emergencyLabel;
    Call m_call;
    MinUi::IconButton *m_speakerButton;
};
}
#endif /* UNLOCK_AGENT_PIN_H_ */