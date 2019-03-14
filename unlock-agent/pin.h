/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include <sailfish-minui/ui.h>
//#include <sailfish-minui/keypad.h>

using namespace Sailfish;

class PinUi : public MinUi::Window
{
public:
    PinUi(MinUi::EventLoop *eventLoop);
    const std::string code();

private:
    MinUi::Icon m_logo{"icon-os-state-update", this};
    MinUi::PasswordField m_pw{this};
    MinUi::Keypad m_key{this};
    //% "Unlock"
    MinUi::Label m_label{qtTrId("label-unlock-id"), this};
};
