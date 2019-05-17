/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include <sailfish-minui/eventloop.h>
#include <sailfish-minui/ui.h>
#include <iostream>
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

PinUi::PinUi(MinUi::EventLoop *eventLoop)
    : MinUi::Window(eventLoop)
    , m_warningLabel(nullptr)
    , m_canShowError(false)
{
    // Imaginary status bar vertical offset.
    int headingVerticalOffset = m_theme.paddingMedium + m_theme.paddingSmall + m_theme.iconSizeExtraSmall;

    // Vertical alignment copied from PinInput.qml of jolla-settings-system

    m_palette.disabled = m_palette.normal;

    m_key.setCancelVisible(false);
    m_key.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_key.setY(window()->height() - m_key.height() - m_theme.paddingLarge);
    m_key.setPalette(m_palette);

    window()->disablePowerButtonSelect();

    // This has dependencies to the m_key
    m_pw.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_pw.setY(std::min(m_key.y(), window()->height() - m_theme.itemSizeSmall) - m_pw.height() - (m_theme.itemSizeSmall / 2));
    m_pw.setPalette(m_palette);

    // This has dependencies to the m_key
    m_label.centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_label.setY(std::min(m_key.y() / 4 + headingVerticalOffset, m_key.y() - m_label.height() - m_theme.paddingMedium));
    m_label.setColor(m_palette.pressed);

    m_key.onKeyPress([this](int code, char character) {
        if (code == ACCEPT_CODE) {
            m_canShowError = true;
            m_timer = window()->eventLoop()->createTimer(16, [this]() {
                disableAll();
                m_callback(m_pw.text());
                window()->eventLoop()->cancelTimer(m_timer);
                m_timer = 0;
            });
        } else if (character && m_timer == 0) {
            if (m_warningLabel) {
                reset();
            }

            m_pw.setText(m_pw.text() + character);
        }
    });
}

MinUi::Label *PinUi::createLabel(const char *name, int y)
{
    MinUi::Label *label = new MinUi::Label(name, this);
    label->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    label->setY(y);
    label->setColor(m_palette.pressed);

    return label;
}

void PinUi::reset()
{
    m_pw.setText("");
    delete m_warningLabel;
    m_warningLabel = nullptr;
}

void PinUi::disableAll()
{
    setEnabled(false);
}

void PinUi::enabledAll()
{
    setEnabled(true);
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

void PinUi::showError()
{
    if (!m_warningLabel && m_canShowError) {
        m_warningLabel = createLabel(m_incorrect_security_code, m_label.y() + m_label.height() + m_theme.paddingLarge);
        enabledAll();
    }
}
