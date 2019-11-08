/*
 * Copyright (C) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#include "agent.h"
#include "call.h"
#include "compositor.h"
#include "devicelocksettings.h"
#include "logging.h"
#include "pin.h"
#include <sailfish-minui-dbus/eventloop.h>
#include <sailfish-minui/display.h>

#define ACCEPT_CODE 28
#define CANCEL_CODE 1
#define UDISKS_DBUS_NAME "org.freedesktop.UDisks2"
#define UDISKS_UUID_PROPERTY "DM_UUID"
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

    delete m_key;
    m_key = nullptr;

    delete m_password;
    m_password = nullptr;

    delete m_busyIndicator;
    m_busyIndicator = nullptr;

    delete m_emergencyButton;
    m_emergencyButton = nullptr;

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
    , m_key(nullptr)
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
    , m_inactivityShutdownEnabled(false)
    , m_inactivityShutdownTimer(0)
    , m_batteryEmptyShutdownRequested(false)
    , m_batteryEmptyShutdownTimer(0)
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

    m_password = new MinUi::PasswordField(this);
    m_key = new MinUi::Keypad(this);
    //% "Enter security code"
    m_label = new MinUi::Label(qtTrId("sailfish-device-encryption-unlock-ui-la-enter_security_code"), this);

    // Imaginary status bar vertical offset.
    int headingVerticalOffset = MinUi::theme.paddingMedium + MinUi::theme.paddingSmall + MinUi::theme.iconSizeExtraSmall;

    // Vertical alignment copied from PinInput.qml of jolla-settings-system

    m_palette.disabled = m_palette.normal;
    m_palette.selected = m_palette.normal;

    m_key->setCancelVisible(false);
    m_key->setAcceptVisible(false);
    m_key->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    m_key->setY(window()->height() - m_key->height() - MinUi::theme.paddingLarge);
    m_key->setPalette(m_palette);

    window()->disablePowerButtonSelect();

    // This has dependencies to the m_key
    m_password->setY(std::min(m_key->y(), window()->height() - MinUi::theme.itemSizeSmall) - m_password->height() - (MinUi::theme.itemSizeSmall / 2));

    // Screen width - we have approximately 2 keys sizes visible as digits are centered - paddings on the edges.
    int margin = (width() - (MinUi::theme.itemSizeHuge * 2.0) - (2 * MinUi::theme.paddingLarge)) / 2;
    m_password->setBold(true);
    m_password->horizontalFill(*this, margin);
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
    m_label->setY(std::min(m_key->y() / 4 + headingVerticalOffset, m_key->y() - m_label->height() - MinUi::theme.paddingMedium));
    m_label->setColor(m_palette.pressed);

    m_busyIndicator = new MinUi::BusyIndicator(this);
    m_busyIndicator->setColor(m_palette.pressed);
    m_busyIndicator->centerBetween(*this, MinUi::Left, *this, MinUi::Right);
    // Should be below the warning text that is created during when error occurs. Height of the warning label is
    // in reality fontSizeSmall but that's currently not exposed from the theme. Hence, iconSizeSmall.
    m_busyIndicator->setY(m_label->y() + m_label->height() + MinUi::theme.iconSizeSmall + 2 * MinUi::theme.paddingLarge);

    m_emergencyButton = new EmergencyButton("icon-encrypt-emergency-call", "icon-encrypt-emergency-call-pressed", this);
    m_emergencyButton->setX(m_label->x());
    m_emergencyButton->setY(m_busyIndicator->y());

    m_emergencyButton->onActivated([this]() {
        setEmergencyMode(!emergencyMode());
    });

    // We have UI -> Enable shutdown on inactivity
    setInactivityShutdownEnabled(true);

    m_key->onKeyPress([this](int code, char character) {
        if (code == ACCEPT_CODE) {
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
        } else if (code == CANCEL_CODE) {
            if (m_call.calling()) {
                // End the ongoing call
                m_call.endCall();
            } else {
                // Cancel pressed, get out of the emergency call mode
                setEmergencyMode(false);
            }
        } else if (character) {
            cancelTimer();
            if (m_warningLabel) {
                reset();
            }
            m_password->setText(m_password->text() + character);
        }

        // Postpone inactivity shutdown
        restartInactivityShutdownTimer();
    });
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
        m_password->setEchoDelay(-1); // Disable number masking

        m_key->setAcceptVisible(false);
        m_key->setCancelVisible(true);
        m_key->setPalette(palette);
        m_key->setAcceptText(m_start_call);

        m_emergencyButton->setVisible(false);
        m_busyIndicator->setColor(palette.normal);
    } else {
        m_call.endCall(); // Just in case
        m_emergencyBackground->setVisible(false);
        m_emergencyLabel->setVisible(false);
        m_label->setVisible(true);
        m_key->setAcceptVisible(false);
        m_key->setCancelVisible(false);
        m_busyIndicator->setColor(m_palette.pressed);

        m_password->setPalette(m_palette);
        m_password->setEchoDelay(100); // Default number masking
        m_key->setPalette(m_palette);
        m_key->setAcceptText(nullptr);
        m_key->setAcceptVisible(false);
        m_emergencyButton->setVisible(true);
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
        m_key->setAcceptVisible(false);
    } else {
        m_key->setAcceptVisible(true);
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
        if (m_key)
            m_key->setVisible(false);
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
            m_key->setAcceptText(m_end_call);
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
            m_key->setAcceptText(m_end_call);
            break;
        case Call::Status::Error:
            destroyWarningLabel();
            m_warningLabel = createLabel(m_emergency_call_failed, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_key->setAcceptText(m_start_call);
            break;
        case Call::Status::InvalidNumber:
            destroyWarningLabel();
            m_warningLabel = createLabel(m_invalid_emergency_number, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            break;
        case Call::Status::Ended:
            destroyWarningLabel();
            m_warningLabel = createLabel(m_emergency_call_ended, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_key->setAcceptText(m_start_call);
            createTimer(EMERGENCY_MODE_TIMEOUT, [this]() {
                setEmergencyMode(false);
            });
            // Fall through to reset speaker status
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
