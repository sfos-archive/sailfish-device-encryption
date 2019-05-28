/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include <sailfish-minui/ui.h>

using namespace Sailfish;

class PinUi : public MinUi::Window
{
public:
    /**
     * Singleton constructor
     */
    static PinUi* instance();

    /**
     * Start the eventloop
     * @param f Callback function for the pin
     * @return Eventloop exit code
     */
    int execute(void (*f)(const std::string&));

    /**
     * Exit the eventloop
     * @param value Exit value
     */
    void exit(int value);

    /**
     * Reset the UI
     */
    void reset();

private:
    PinUi(MinUi::EventLoop *eventLoop);

private:
    MinUi::PasswordField m_pw { this };
    MinUi::Keypad m_key { this };

    //% "Enter security code"
    MinUi::Label m_label { qtTrId("sailfish-device-encryption-unlock-ui-la-enter_security_code"), this };

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

    MinUi::EventLoop* m_eventLoop;
    void (*m_callback)(const std::string&);
    int m_timer;
};
