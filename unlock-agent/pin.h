/*
 * Copyright (C) 2019 Jolla Ltd
 */

#ifndef UNLOCK_AGENT_PIN_H_
#define UNLOCK_AGENT_PIN_H_
#include <sailfish-minui/ui.h>
#include <sailfish-minui/eventloop.h>
#include <sailfish-mindbus/object.h>

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
    PinUi(MinUi::EventLoop *eventLoop);
    virtual ~PinUi();
    MinUi::Label *createLabel(const char *name, int y);

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
    void watchForDBusChanges();
    virtual void displayStateChanged();
    virtual void updatesEnabledChanged();
    void sendPassword(const std::string& password);
    void startAskWatcher();
    static bool askWatcher(int descriptor, uint32_t events);

private:
    MinUi::PasswordField *m_password;
    MinUi::Keypad *m_key;
    MinUi::Label *m_label;
    MinUi::Label *m_warningLabel;

    // Placeholder strings for error handling

    //% "Incorrect security code"
    const char *m_incorrect_security_code { qtTrId("sailfish-device-encryption-unlock-ui-la-incorrect_security_code") };

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

    MinUi::Palette m_palette;
    MinUi::Theme m_theme;
    int m_timer;
    bool m_canShowError;
    bool m_createdUI;
    bool m_displayOn;
    bool m_checkTemporaryKey;
    static PinUi *s_instance;
    MinDBus::Object *m_dbus;
    const char *m_socket;
};
}
#endif /* UNLOCK_AGENT_PIN_H_ */
