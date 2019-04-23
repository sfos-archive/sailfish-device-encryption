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
    MinUi::Icon m_logo{"icon-os-state-update", this};
    MinUi::PasswordField m_pw{this};
    MinUi::Keypad m_key{this};
    //% "Unlock"
    MinUi::Label m_label{qtTrId("label-unlock-id"), this};
    MinUi::EventLoop* m_eventLoop;
    MinUi::Image* m_image;
    void (*m_callback)(const std::string&);
    int m_timer;
};
