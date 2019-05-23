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

static PinUi* m_instance = NULL;

PinUi* PinUi::instance()
{
    if (!m_instance) {
        static MinUi::EventLoop eventLoop;
        m_instance = new PinUi(&eventLoop);
    }
    return m_instance;
}

PinUi::PinUi(MinUi::EventLoop *eventLoop) : MinUi::Window(eventLoop)
{
    MinUi::Theme theme;

    // Imaginary status bar vertical offset.
    int headingVerticalOffset = theme.paddingMedium + theme.paddingSmall + theme.iconSizeExtraSmall;

    // Vertical alignment copied from PinInput.qml of jolla-settings-system

    m_key.setCancelVisible(false);
    m_key.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_key.setY(window()->height() - m_key.height() - theme.paddingLarge);

    window()->disablePowerButtonSelect();

    // This has dependencies to the m_key
    m_pw.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_pw.setY(std::min(m_key.y(), window()->height() - theme.itemSizeSmall) - m_pw.height() - (theme.itemSizeSmall / 2));

    // This has dependencies to the m_key
    m_label.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_label.setY(std::min(m_key.y() / 4 + headingVerticalOffset, m_key.y() - m_label.height() - theme.paddingMedium));
    MinUi::Palette palette;
    m_label.setColor(palette.pressed);

    m_image = NULL;

    m_key.onKeyPress([this](int code, char character) {
        if (code == ACCEPT_CODE) {
            // Fade and exit
            m_timer = window()->eventLoop()->createTimer(10, [this]() {
                double opacity = m_key.opacity() - 0.02;
                if (opacity > 0.02) {
                    m_key.setOpacity(opacity);
                    m_label.setOpacity(opacity);
                    m_pw.setOpacity(opacity);
                } else {
                    window()->eventLoop()->cancelTimer(m_timer);
                    m_key.setOpacity(0.0);
                    m_label.setOpacity(0.0);
                    m_pw.setOpacity(0.0);
                    // Call the callback
                    m_callback(m_pw.text());
                }
            });
        } else if (character) m_pw.setText(m_pw.text() + character);
    });
}

void PinUi::reset()
{
    m_key.setOpacity(1.0);
    m_label.setOpacity(1.0);
    m_pw.setOpacity(1.0);
    m_pw.setText("");
    if (m_image) {
        delete m_image;
        m_image = NULL;
    }
}

int PinUi::execute(void (*f)(const std::string&))
{
    m_callback = f;
    return window()->eventLoop()->execute();
}

void PinUi::exit(int value)
{
    window()->eventLoop()->exit(value);
}
