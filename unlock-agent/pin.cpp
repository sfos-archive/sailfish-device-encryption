/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include "pin.h"
#include "logging.h"
#include "devicelocksettings.h"
#include <sailfish-minui-dbus/eventloop.h>

#define ACCEPT_CODE 28

using namespace Sailfish;

PinUi* PinUi::s_instance = nullptr;

PinUi* PinUi::instance()
{
    if (!s_instance) {
        static MinUi::DBus::EventLoop eventLoop;
        s_instance = new PinUi(&eventLoop);
    }
    return s_instance;
}

PinUi::~PinUi()
{
    window()->eventLoop()->cancelTimer(m_timer);
    m_timer = 0;

    delete m_warningLabel;
    m_warningLabel = nullptr;

    delete m_label;
    m_label = nullptr;

    delete m_key;
    m_key = nullptr;

    delete m_password;
    m_password = nullptr;
}

PinUi::PinUi(MinUi::EventLoop *eventLoop)
    : MinUi::Window(eventLoop)
    , Compositor(eventLoop)
    , m_password(nullptr)
    , m_key(nullptr)
    , m_label(nullptr)
    , m_warningLabel(nullptr)
    , m_callback(nullptr)
    , m_timer(0)
    , m_canShowError(false)
    , m_createdUI(false)
    , m_displayOn(true) // because minui unblanks screen on startup
{
}

void PinUi::createUI()
{
    if (m_createdUI)
        return;

    log_debug("create ui");
    m_createdUI = true;

    setBlankPreventWanted(true);

    m_password = new MinUi::PasswordField(this);
    m_key = new MinUi::Keypad(this);
    //% "Enter security code"
    m_label = new MinUi::Label(qtTrId("sailfish-device-encryption-unlock-ui-la-enter_security_code"), this);

    // Imaginary status bar vertical offset.
    int headingVerticalOffset = m_theme.paddingMedium + m_theme.paddingSmall + m_theme.iconSizeExtraSmall;

    // Vertical alignment copied from PinInput.qml of jolla-settings-system

    m_palette.disabled = m_palette.normal;

    m_key->setCancelVisible(false);
    m_key->setAcceptVisible(false);
    m_key->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_key->setY(window()->height() - m_key->height() - m_theme.paddingLarge);
    m_key->setPalette(m_palette);

    window()->disablePowerButtonSelect();

    // This has dependencies to the m_key
    m_password->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_password->setY(std::min(m_key->y(), window()->height() - m_theme.itemSizeSmall) - m_password->height() - (m_theme.itemSizeSmall / 2));
    m_password->setPalette(m_palette);
    m_password->setMaximumLength(DeviceLockSettings::instance()->maximumCodeLength());
    m_password->onTextChanged([this](MinUi::TextInput::Reason reason) {
        if (reason == MinUi::TextInput::Deletion) {
            if (m_warningLabel) {
                reset();
            }
        }
        updateAcceptVisibility();
    });

    // This has dependencies to the m_key
    m_label->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_label->setY(std::min(m_key->y() / 4 + headingVerticalOffset, m_key->y() - m_label->height() - m_theme.paddingMedium));
    m_label->setColor(m_palette.pressed);

    m_key->onKeyPress([this](int code, char character) {
        if (code == ACCEPT_CODE) {
            m_canShowError = true;
            m_timer = window()->eventLoop()->createTimer(16, [this]() {
                disableAll();
                m_callback(m_password->text());
                window()->eventLoop()->cancelTimer(m_timer);
                m_timer = 0;
            });
        } else if (character && m_timer == 0) {
            if (m_warningLabel) {
                reset();
            }
            m_password->setText(m_password->text() + character);
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
    if (!m_createdUI)
        return;

    m_password->setText("");
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

void PinUi::updateAcceptVisibility()
{
    if (!m_createdUI)
        return;

    if (m_password->text().length() < DeviceLockSettings::instance()->minimumCodeLength()) {
        m_key->setAcceptVisible(false);
    } else {
        m_key->setAcceptVisible(true);
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

void PinUi::showError()
{
    if (!m_createdUI)
        return;

    if (!m_warningLabel && m_canShowError) {
        m_warningLabel = createLabel(m_incorrect_security_code, m_label->y() + m_label->height() + m_theme.paddingLarge);
        enabledAll();
    }
}

void PinUi::displayStateChanged()
{
    /* While input policy is not implemented (things like
     * event eater in dimmed display state etc), we do not
     * need to care about display state.
     */
}
void PinUi::updatesEnabledChanged()
{
    bool prev = m_displayOn;
    m_displayOn = updatesEnabled();

    if (!m_displayOn) {
        log_debug("hide");
        window()->setVisible(false);
    }

    if (prev != m_displayOn) {
        if (m_displayOn) {
            log_debug("unblank");
            gr_fb_blank(false);
        } else {
            log_debug("blank");
            gr_fb_blank(true);
        }
    }

    if (m_displayOn) {
        log_debug("show");
        window()->setVisible(true);
        createUI();
    }
}
