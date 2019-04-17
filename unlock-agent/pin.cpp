/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include <sailfish-minui/eventloop.h>
#include <sailfish-minui/ui.h>
#include <sailfish-mindbus/object.h>
#include <mce/dbus-names.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "pin.h"

#define ACCEPT_CODE 28

// Values taken from startupwizard screenblank.cpp
#define BLANKING_TIMEOUT 10000
#define MCE_NOTIFICATION_ID "sailfish-unlock-ui"
#define MCE_NOTIFICATION_DURATION 30000
#define MCE_NOTIFICATION_EXTEND 5000

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
                } else if (opacity > 0.0) {
                    m_key.setOpacity(opacity);
                    m_label.setOpacity(opacity);
                    m_pw.setOpacity(opacity);
                    // Image needs one event loop to show up
                    m_image = new MinUi::Image("/usr/share/themes/sailfish-default/meegotouch/icons/graphic-shutdown-480x854.png", this);
                    m_image->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
                    m_image->align(MinUi::Top, m_logo, MinUi::Bottom, 0);
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

    // Timer to keep display from blanking
    eventLoop->createTimer(BLANKING_TIMEOUT, [this]() {
        MinDBus::Object mce {MinDBus::systemBus(), MCE_SERVICE,
            MCE_REQUEST_PATH, MCE_REQUEST_IF};
        mce.call(MCE_NOTIFICATION_BEGIN_REQ, MCE_NOTIFICATION_ID,
            MCE_NOTIFICATION_DURATION, MCE_NOTIFICATION_EXTEND);
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
