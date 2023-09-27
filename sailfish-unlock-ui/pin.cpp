/****************************************************************************************
** Copyright (c) 2019 - 2023 Jolla Ltd.
** Copyright (c) 2019 Open Mobile Platform LLC.
**
** All rights reserved.
**
** This file is part of Sailfish Device Encryption package.
**
** You may use this file under the terms of BSD license as follows:
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
**    list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
****************************************************************************************/

#include "agent.h"
#include "call.h"
#include "compositor.h"
#include "devicelocksettings.h"
#include "logging.h"
#include "pin.h"
#include <sailfish-minui-dbus/eventloop.h>
#include <sailfish-minui/display.h>

#define EMERGENCY_MODE_TIMEOUT 5000

using namespace Sailfish;

static const MinUi::Color color_red(255, 0, 0, 255);
static const MinUi::Color color_lightred(200, 0, 0, 255);
static const MinUi::Color color_reddish(76, 0, 0, 255);
static const MinUi::Color color_white(255, 255, 255, 255);

PinUi::~PinUi()
{
    cancelTimer();

    stopInactivityShutdownTimer();
    cancelBatteryEmptyShutdown();

    destroyWarningLabel();

    delete m_label;
    m_label = nullptr;

    delete m_keypad;
    m_keypad = nullptr;

    delete m_keyboard;
    m_keyboard = nullptr;

    delete m_password;
    m_password = nullptr;

    delete m_busyIndicator;
    m_busyIndicator = nullptr;

    delete m_emergencyButton;
    m_emergencyButton = nullptr;

    delete m_keypadButton;
    m_keypadButton = nullptr;

    delete m_keyboardButton;
    m_keyboardButton = nullptr;

    delete m_emergencyBackground;
    m_emergencyBackground = nullptr;

    delete m_emergencyLabel;
    m_emergencyLabel = nullptr;

    delete m_speakerButton;
    m_speakerButton = nullptr;

    delete m_exitNotification;
    m_exitNotification = nullptr;
}

PinUi::PinUi(Agent *agent, MinUi::DBus::EventLoop *eventLoop)
    : MinUi::Window(eventLoop)
    , m_agent(agent)
    , m_password(nullptr)
    , m_keyboard(nullptr)
    , m_keypad(nullptr)
    , m_label(nullptr)
    , m_warningLabel(nullptr)
    , m_busyIndicator(nullptr)
    , m_timer(0)
    , m_canShowError(false)
    , m_displayOn(true) // because minui unblanks screen on startup
    , m_dbus(nullptr)
    , m_emergencyButton(nullptr)
    , m_emergencyMode(false)
    , m_emergencyBackground(nullptr)
    , m_emergencyLabel(nullptr)
    , m_call(Call(eventLoop))
    , m_speakerButton(nullptr)
    , m_keypadButton(nullptr)
    , m_keyboardButton(nullptr)
    , m_inactivityShutdownEnabled(false)
    , m_inactivityShutdownTimer(0)
    , m_batteryEmptyShutdownRequested(false)
    , m_batteryEmptyShutdownTimer(0)
    , m_keypadInUse(true)
    , m_exitNotification(nullptr)
{
    m_agent->setBlankPreventWanted(true);

    // Emergency mode rectangle
    m_emergencyBackground = new MinUi::Rectangle(this);
    m_emergencyBackground->setVisible(false);
    m_emergencyBackground->setEnabled(false);
    m_emergencyBackground->setX(0);
    m_emergencyBackground->setY(0);
    m_emergencyBackground->setWidth(width());
    m_emergencyBackground->setHeight(height());
    m_emergencyBackground->setColor(color_reddish);

    m_emergencyButton = new EmergencyButton("icon-encrypt-emergency-call", "icon-encrypt-emergency-call-pressed", this);
    m_keypadButton = new MinUi::IconButton("icon-m-encryption-dialpad", this);
    m_keyboardButton = new MinUi::IconButton("icon-m-encryption-keyboard", this);

    m_password = new MinUi::PasswordField(this);
    m_keypad = new MinUi::Keypad(this);

    //% "Enter security code"
    m_label = new MinUi::Label(qtTrId("sailfish-device-encryption-unlock-ui-la-enter_security_code"), this);

    // Imaginary status bar vertical offset.
    int headingVerticalOffset = MinUi::theme.paddingMedium + MinUi::theme.paddingSmall + MinUi::theme.iconSizeExtraSmall;

    // Vertical alignment copied from PinInput.qml of jolla-settings-system

    m_palette.disabled = m_palette.normal;
    m_palette.selected = m_palette.normal;

    m_keypad->setCancelVisible(false);
    m_keypad->setAcceptVisible(false);
    m_keypad->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_keypad->setY(window()->height() - m_keypad->height() - MinUi::theme.paddingMedium - window()->height()/20);
    m_keypad->setPalette(m_palette);

    m_keyboard = new MinUi::Keyboard(this);
    m_keyboard->align(MinUi::Bottom, *this, MinUi::Bottom);
    m_keyboard->horizontalFill(*this);

    window()->disablePowerButtonSelect();

    int inputY = std::min(m_keypad->y(), m_keyboard->y());
    m_password->setY(inputY - m_password->height() - (MinUi::theme.itemSizeSmall / 2));

    // Screen width - we have approximately 2 keys sizes visible as digits are centered - paddings on the edges.
    int margin = (width() - (MinUi::theme.itemSizeHuge * 2.0) - (2 * MinUi::theme.paddingLarge)) / 2;
    m_password->setBold(true);
    m_password->horizontalFill(*this, margin);
    m_password->setPalette(m_palette);
    m_password->setMaximumLength(DeviceLockSettings::instance()->maximumCodeLength());
    m_password->onTextChanged([this](MinUi::TextInput::Reason reason) {
        m_emergencyButton->setVisible(m_password->text().size() < 5 && !emergencyMode());

        if (reason == MinUi::TextInput::Deletion) {
            if (m_warningLabel) {
                reset();
            }
        }
        updateAcceptVisibility();
    });

    m_label->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_label->setY(std::min(inputY / 4 + headingVerticalOffset,
                           inputY - m_label->height() - MinUi::theme.paddingMedium));
    m_label->setColor(m_palette.pressed);

    m_busyIndicator = new MinUi::BusyIndicator(this);
    m_busyIndicator->setColor(m_palette.pressed);
    m_busyIndicator->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_busyIndicator->centerBetween(*m_label, MinUi::Bottom, *m_password, MinUi::Top);

    m_emergencyButton->setX(margin);
    m_emergencyButton->centerBetween(*m_password, MinUi::Top, *m_password, MinUi::Bottom);

    m_emergencyButton->onActivated([this]() {
        setEmergencyMode(!emergencyMode());

        m_emergencyButton->setVisible(m_password->text().size() < 5 && !emergencyMode());
    });

    m_keypadButton->setX(MinUi::theme.paddingLarge);
    m_keypadButton->setY(MinUi::theme.paddingLarge);

    m_keyboardButton->setX(MinUi::theme.paddingLarge);
    m_keyboardButton->setY(MinUi::theme.paddingLarge);

    m_keypadButton->onActivated([this]() {
       m_keypadInUse = true;
       updateInputItemVisibilities();
    });

    m_keyboardButton->onActivated([this]() {
       m_keypadInUse = false;
       updateInputItemVisibilities();
    });

    // We have UI -> Enable shutdown on inactivity
    setInactivityShutdownEnabled(true);

    // FIXME: ideally the text field would handle the input
    m_keypad->onKeyPress([this](int code, char character){ handleKeyPress(code, character); });
    m_keyboard->onKeyPress([this](int code, char character){ handleKeyPress(code, character); });

    updateInputItemVisibilities();
}

bool PinUi::emergencyMode() const
{
    return m_emergencyMode;
}

void PinUi::setEmergencyMode(bool emergency)
{
    if (m_emergencyMode != emergency) {
        m_emergencyMode = emergency;
        log_debug("m_emergencyMode: " << m_emergencyMode);
        emergencyModeChanged();
    }
}

void PinUi::emergencyModeChanged()
{
    /* Handle UI changes */
    cancelTimer();
    m_password->setText("");
    m_password->setMaskingEnabled(!emergencyMode());
    m_emergencyButton->setVisible(!emergencyMode());
    updateInputItemVisibilities();

    if (emergencyMode()) {
        m_emergencyBackground->setVisible(true);
        m_label->setVisible(false);

        if (!m_emergencyLabel) {
            //% "Emergency call"
            m_emergencyLabel = new MinUi::Label(qtTrId("sailfish-device-encryption-unlock-ui-la-emergency_call"), this);
            m_emergencyLabel->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
            m_emergencyLabel->setY(m_label->y());
            m_emergencyLabel->setColor(color_red);
        } else {
            m_emergencyLabel->setVisible(true);
        }

        MinUi::Palette palette;
        palette.normal = color_red;
        palette.selected = color_red;
        palette.disabled = color_red;
        palette.pressed = color_white;

        m_password->setPalette(palette);
        m_keypad->setAcceptVisible(false);
        m_keypad->setCancelVisible(true);
        m_keypad->setPalette(palette);
        m_keypad->setAcceptText(m_start_call);

        m_busyIndicator->setColor(palette.normal);
    } else {
        m_call.endCall(); // Just in case
        m_emergencyBackground->setVisible(false);
        m_emergencyLabel->setVisible(false);
        m_label->setVisible(true);
        m_keypad->setAcceptVisible(false);
        m_keypad->setCancelVisible(false);
        m_busyIndicator->setColor(m_palette.pressed);

        m_password->setPalette(m_palette);
        m_keypad->setPalette(m_palette);
        m_keypad->setAcceptText(nullptr);
        m_keypad->setAcceptVisible(false);
        if (m_speakerButton)
            m_speakerButton->setVisible(false);

        destroyWarningLabel();
    }

    /* Handle shutdown policy */
    if (emergencyMode())
        stopInactivityShutdownTimer();
    else
        startInactivityShutdownTimer();
    considerBatteryEmptyShutdown();
}

bool PinUi::inactivityShutdownEnabled(void) const
{
    return m_inactivityShutdownEnabled;
}

void PinUi::setInactivityShutdownEnabled(bool enabled)
{
    if (m_inactivityShutdownEnabled != enabled) {
        m_inactivityShutdownEnabled = enabled;
        log_debug("m_inactivityShutdownEnabled: " << m_inactivityShutdownEnabled);

        if (inactivityShutdownEnabled())
            startInactivityShutdownTimer();
        else
            stopInactivityShutdownTimer();
    }
}
void PinUi::stopInactivityShutdownTimer()
{
    if (m_inactivityShutdownTimer) {
        log_debug("inactivity shutdown timer: stopped");
        eventLoop()->cancelTimer(m_inactivityShutdownTimer);
        m_inactivityShutdownTimer = 0;
    }
}

void PinUi::startInactivityShutdownTimer()
{
    if (m_inactivityShutdownTimer) {
        /* Already running */
    } else if (m_agent->targetUnitActive()) {
        /* Yield to DSME side shutdown policy */
    } else if (!inactivityShutdownEnabled()) {
        /* Disabled by UI logic */
    } else if (emergencyMode()) {
        /* Disabled by ongoing emergency call */
    } else if (m_agent->chargerState() == Compositor::ChargerState::ChargerOn) {
        /* Does not make sense when charger is connected */
    } else if (m_agent->dsmeState() != Compositor::DsmeState::DSME_STATE_USER) {
        /* Already rebooting / shutting down / something */
    } else {
        log_debug("inactivity shutdown timer: started");
        m_inactivityShutdownTimer =
            eventLoop()->createTimer(inactivityShutdownDelay, [this]() {
                stopInactivityShutdownTimer();
                onInactivityShutdownTimer();
            });
    }
}

void PinUi::restartInactivityShutdownTimer()
{
    // Always stop currently scheduled timeout
    stopInactivityShutdownTimer();
    // When allowed, start new timeout
    startInactivityShutdownTimer();
}

void PinUi::onInactivityShutdownTimer()
{
    log_debug("inactivity shutdown timer: triggered");
    setInactivityShutdownEnabled(false);
    m_agent->sendShutdownRequestToDsme();
}

void PinUi::considerBatteryEmptyShutdown()
{
    /* Normally DSME takes care of battery empty shutdown,
     *
     * However, the dsme side policy is disable during
     * device bootup, and we are effectively pausing the
     * startup sequence for unknown length of time to wait
     * for user input.
     *
     * This basically means that we need to implement
     * custom battery empty policy within unlock-ui and
     * shutdown when battery is too low to successfully
     * finish booting up.
     */

    bool wantTimer = false;

    if (m_batteryEmptyShutdownRequested) {
        /* Already requested */
    } else if (m_agent->targetUnitActive()) {
        /* Yield to DSME side shutdown policy */
    } else if (m_agent->batteryStatus() != Compositor::BatteryStatus::BatteryEmpty) {
        /* Battery is not empty */
    } else if (emergencyMode()) {
        /* Disabled by ongoing emergency call */
    } else if (m_agent->chargerState() != Compositor::ChargerState::ChargerOff) {
        /* Charger state is not known to be offline
         *
         * Note that we must not shutdown while charger
         * state is unknown.
         *
         * Additionally a bit of hysteresis is needed for
         * offline case too to allow charger detection some
         * time to settle down to stable state in the early
         * bootup - handled via 10 second timer delay below.
         */
    } else if (m_agent->dsmeState() != Compositor::DsmeState::DSME_STATE_USER) {
        /* Already rebooting / shutting down / something */
    } else {
        wantTimer = true;
    }

    bool haveTimer = (m_batteryEmptyShutdownTimer != 0);

    if (haveTimer != wantTimer) {
        if (wantTimer) {
            log_debug("battery empty shutdown timer: started");
            m_batteryEmptyShutdownTimer =
                eventLoop()->createTimer(batteryEmptyShutdownDelay, [this]() {
                    cancelBatteryEmptyShutdown();
                    onBatteryEmptyShutdown();
                });
        } else {
            cancelBatteryEmptyShutdown();
        }
    }
}

void PinUi::cancelBatteryEmptyShutdown()
{
    if (m_batteryEmptyShutdownTimer) {
        log_debug("battery empty shutdown timer: stopped");
        eventLoop()->cancelTimer(m_batteryEmptyShutdownTimer);
        m_batteryEmptyShutdownTimer = 0;
    }
}

void PinUi::onBatteryEmptyShutdown()
{
    log_warning("battery empty shutdown timer: triggered");

    /* We want to do this only once */
    m_batteryEmptyShutdownRequested = true;

    /* Send shutdown request */
    m_agent->sendShutdownRequestToDsme();
}

void PinUi::destroyWarningLabel()
{
    delete m_warningLabel;
    m_warningLabel = nullptr;
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
    m_password->setText("");
    destroyWarningLabel();
}

void PinUi::disableAll()
{
    setInactivityShutdownEnabled(false);
    setEnabled(false);
    if (m_busyIndicator) {
        m_busyIndicator->setRunning(true);
    }
}

void PinUi::enabledAll()
{
    setEnabled(true);
    if (m_busyIndicator) {
        m_busyIndicator->setRunning(false);
    }
    setInactivityShutdownEnabled(true);
}

void PinUi::updateAcceptVisibility()
{
    if (emergencyMode())
        return;

    if (m_password->text().length() < DeviceLockSettings::instance()->minimumCodeLength()) {
        m_keypad->setAcceptVisible(false);
        m_keyboard->setEnterEnabled(false);
    } else {
        m_keypad->setAcceptVisible(true);
        m_keyboard->setEnterEnabled(true);
    }
}

void PinUi::newAskFile()
{
    // New file moved to the ask directory, assume password failed
    if (!emergencyMode())
        showError();
}

void PinUi::showError()
{
    reset();
    if (!m_warningLabel && m_canShowError) {
        //% "Incorrect security code"
        m_warningLabel = createLabel(qtTrId("sailfish-device-encryption-unlock-ui-la-incorrect_security_code"), m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
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

void PinUi::chargerStateChanged()
{
    if (m_agent->chargerState() == Compositor::ChargerState::ChargerOn)
        stopInactivityShutdownTimer();
    else
        startInactivityShutdownTimer();

    considerBatteryEmptyShutdown();
}

void PinUi::batteryStatusChanged()
{
    considerBatteryEmptyShutdown();
}

void PinUi::batteryLevelChanged()
{
    /* TODO: act-dead charging UI does not allow
     *       attempts to boot up to USER state
     *       unles battery level >= 3% - should
     *       we implement something similar here?
     */
}

void PinUi::dsmeStateChanged()
{
    /* Re-evaluate shutdown conditions */
    switch (m_agent->dsmeState())
    {
    case Compositor::DsmeState::DSME_STATE_USER:
        startInactivityShutdownTimer();
        considerBatteryEmptyShutdown();
        break;
    default:
        stopInactivityShutdownTimer();
        cancelBatteryEmptyShutdown();
        m_batteryEmptyShutdownRequested = false;
        break;
    }

    /* Re-evaluate UI state */
    if (m_agent->shuttingDown() && !m_exitNotification) {
        // Hide everything
        if (m_password)
            m_password->setVisible(false);
        if (m_keypad)
            m_keypad->setVisible(false);
        if (m_keyboard)
            m_keyboard->setVisible(false);
        if (m_label)
            m_label->setVisible(false);
        if (m_warningLabel)
            m_warningLabel->setVisible(false);
        if (m_busyIndicator)
            m_busyIndicator->setVisible(false);
        if (m_emergencyButton)
            m_emergencyButton->setVisible(false);
        if (m_emergencyBackground)
            m_emergencyBackground->setVisible(false);
        if (m_emergencyLabel)
            m_emergencyLabel->setVisible(false);
        if (m_speakerButton)
            m_speakerButton->setVisible(false);
        // Show either reboot or poweroff notification
        if (m_agent->shuttingDownToReboot()) {
            //% "One moment..."
            m_exitNotification = new MinUi::Label(qtTrId("sailfish-device-encryption-unlock-ui-la-rebooting"), this);
        } else {
            //% "Goodbye!"
            m_exitNotification = new MinUi::Label(qtTrId("sailfish-device-encryption-unlock-ui-la-goodbye"), this);
        }
        m_exitNotification->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
        m_exitNotification->centerBetween(*this, MinUi::Top, *this, MinUi::Bottom);
    }
}

void PinUi::targetUnitActiveChanged()
{
    if (m_agent->targetUnitActive())
        stopInactivityShutdownTimer();
    else
        startInactivityShutdownTimer();
    considerBatteryEmptyShutdown();
}

void PinUi::updatesEnabledChanged()
{
    bool prev = m_displayOn;
    m_displayOn = m_agent->updatesEnabled();

    if (!m_displayOn) {
        log_debug("hide");
        window()->setVisible(false);
    }

    if (prev != m_displayOn) {
        if (m_displayOn) {
            log_debug("unblank");
            MinUi::Display::instance()->unblank();
        } else {
            log_debug("blank");
            MinUi::Display::instance()->blank();
        }
    }

    if (m_displayOn) {
        log_debug("show");
        window()->setVisible(true);
    }
}

void PinUi::setEmergencyCallStatus(Call::Status status)
{
    if (!emergencyMode()) {
        // Not in emergency mode, do nothing
        return;
    }

    switch (status) {
        case Call::Status::Initializing:
            m_busyIndicator->setRunning(true);
            destroyWarningLabel();
            m_warningLabel = createLabel(m_calling_emergency, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_keypad->setAcceptText(m_end_call);
            // Cancel possible emergency mode timeout
            cancelTimer();
            break;
        case Call::Status::Calling:
            m_busyIndicator->setRunning(false);

            if (!m_speakerButton) {
                MinUi::Palette palette;
                palette.normal = color_red;
                palette.selected = color_red;
                palette.disabled = color_red;
                palette.pressed = color_white;

                m_speakerButton = new MinUi::IconButton("icon-m-speaker", this);
                m_speakerButton->centerBetween(*this, MinUi::Left, *this, MinUi::Right);;
                m_speakerButton->setY(m_label->y() + m_label->height() + MinUi::theme.iconSizeSmall + 2 * MinUi::theme.paddingLarge);
                m_speakerButton->setPalette(palette);

                m_speakerButton->onActivated([this]() {
                    m_call.toggleSpeaker();
                });
            } else {
                m_speakerButton->setVisible(true);
            }

            destroyWarningLabel();
            m_warningLabel = createLabel(m_calling_emergency, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_keypad->setAcceptText(m_end_call);
            break;
        case Call::Status::Error:
            m_busyIndicator->setRunning(false);
            destroyWarningLabel();
            m_warningLabel = createLabel(m_emergency_call_failed, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_keypad->setAcceptText(m_start_call);
            break;
        case Call::Status::InvalidNumber:
            m_busyIndicator->setRunning(false);
            destroyWarningLabel();
            m_warningLabel = createLabel(m_invalid_emergency_number, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_keypad->setAcceptText(m_start_call);
            break;
        case Call::Status::Ended:
            m_busyIndicator->setRunning(false);
            destroyWarningLabel();
            m_warningLabel = createLabel(m_emergency_call_ended, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_keypad->setAcceptText(m_start_call);
            createTimer(EMERGENCY_MODE_TIMEOUT, [this]() {
                setEmergencyMode(false);
            });
            [[fallthrough]]; // fall-through to reset speaker status
        case Call::Status::EarpieceOn: {
            if (m_speakerButton) {
                // Speaker disabled
                MinUi::Palette palette = m_speakerButton->palette();
                palette.normal = color_red;
                palette.selected = color_red;
                palette.pressed = color_white;
                m_speakerButton->setPalette(palette);
            }
            break;
        }
        case Call::Status::SpeakerOn: {
            if (m_speakerButton) {
                // Speaker enabled
                MinUi::Palette palette = m_speakerButton->palette();
                palette.normal = color_white;
                palette.selected = color_white;
                palette.pressed = color_red;
                m_speakerButton->setPalette(palette);
            }
            break;
        }
        default:
            break;
    }
    if (m_warningLabel)
        m_warningLabel->setColor(color_white);
}

void PinUi::createTimer(int interval, const std::function<void()> &callback)
{
    cancelTimer();
    m_timer = eventLoop()->createTimer(interval, callback);
}

void PinUi::cancelTimer()
{
    if (m_timer) {
        eventLoop()->cancelTimer(m_timer);
        m_timer = 0;
    }
}

void PinUi::handleKeyPress(int code, char character)
{
    if (code == KEY_ENTER) {
        if (emergencyMode()) {
            if (m_call.calling()) {
                // End the ongoing call
                m_call.endCall();
            } else {
                // Make the call
                std::string number = m_password->text();
                m_call.makeCall(number, [this](Call::Status status) {
                    setEmergencyCallStatus(status);
                });
            }
        } else {
            // Send the password
            m_canShowError = true;
            createTimer(16, [this]() {
                cancelTimer();
                disableAll();
                m_agent->sendPassword(m_password->text());
            });
        }
    } else if (code == KEY_ESC) {
        if (m_call.calling()) {
            // End the ongoing call
            m_call.endCall();
        } else {
            // Cancel pressed, get out of the emergency call mode
            setEmergencyMode(false);
        }
    } else if (code == KEY_BACKSPACE) {
        m_password->backspace();
    } else if (character) {
        cancelTimer();
        if (m_warningLabel) {
            reset();
        }
        m_password->setText(m_password->text() + character);
    }

    // Postpone inactivity shutdown
    restartInactivityShutdownTimer();
}

void PinUi::updateInputItemVisibilities()
{
    m_keypadButton->setVisible(!emergencyMode() && !m_keypadInUse);
    m_keyboardButton->setVisible(!emergencyMode() && m_keypadInUse);
    if (!emergencyMode() && m_keypadInUse) {
        m_password->setMaskingEnabled(true);
    }

    bool keypadVisible = emergencyMode() || m_keypadInUse;
    m_keypad->setVisible(keypadVisible);
    m_keyboard->setVisible(!keypadVisible);

    m_password->setExtraButtonMode(keypadVisible ? MinUi::PasswordField::ShowBackspace
                                                 : MinUi::PasswordField::ShowTextVisibilityToggle);
}
