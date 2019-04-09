/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include <sailfish-minui/eventloop.h>
#include <sailfish-minui/ui.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "pin.h"

#define ACCEPT_CODE 28

using namespace Sailfish;

PinUi::PinUi(MinUi::EventLoop *eventLoop) : MinUi::Window(eventLoop)
{
    int margin = (window()->height() / 2) - m_key.height();

    m_logo.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_logo.align(MinUi::Top, *this, MinUi::Top, margin);

    m_label.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_label.align(MinUi::Top, m_logo, MinUi::Bottom, 50);

    m_key.setCancelVisible(false);
    m_key.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_key.align(MinUi::Bottom, *this, MinUi::Bottom, -margin);

    m_pw.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_pw.align(MinUi::Bottom, m_key, MinUi::Top, -10);

    m_key.onKeyPress([this](int code, char character) {
        if (code == ACCEPT_CODE) {
            // Fade and exit
            window()->eventLoop()->createTimer(10, [this]() {
                double opacity = m_key.opacity() - 0.02;
                if (opacity > 0.02) {
                    m_key.setOpacity(opacity);
                    m_label.setOpacity(opacity);
                    m_pw.setOpacity(opacity);
                } else if (opacity > 0.0) {
                    m_key.setOpacity(opacity);
                    m_label.setOpacity(opacity);
                    m_pw.setOpacity(opacity);
                    // Image needs one event loop to show up
                    MinUi::Image* image = new MinUi::Image("/usr/share/themes/sailfish-default/meegotouch/icons/graphic-shutdown-480x854.png", this);
                    image->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
                    image->align(MinUi::Top, m_logo, MinUi::Bottom, 0);
                } else {
                    m_key.setOpacity(0.0);
                    m_label.setOpacity(0.0);
                    m_pw.setOpacity(0.0);
                    window()->eventLoop()->exit(0);
                }
            });
        } else if (character) m_pw.setText(m_pw.text() + character);
    });
}

const std::string PinUi::code()
{
    return m_pw.text();
}