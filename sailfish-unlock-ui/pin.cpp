/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include "pin.h"
#include "call.h"
#include "logging.h"
#include "devicelocksettings.h"
#include <sailfish-minui-dbus/eventloop.h>

#include <unistd.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#define ACCEPT_CODE 28
#define CANCEL_CODE 1
#define UDISKS_DBUS_NAME "org.freedesktop.UDisks2"
#define UDISKS_UUID_PROPERTY "DM_UUID"
#define TEMPORARY_KEY_FILE \
    "/var/lib/sailfish-device-encryption/temporary-encryption-key"
#define TEMPORARY_KEY "00000"

static inline bool temporary_key_is_set();

using namespace Sailfish;

static const MinUi::Color color_red(255, 0, 0, 255);
static const MinUi::Color color_lightred(200, 0, 0, 255);
static const MinUi::Color color_reddish(76, 0, 0, 255);
static const MinUi::Color color_white(255, 255, 255, 255);

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
}

PinUi::PinUi(MinUi::DBus::EventLoop *eventLoop)
    : MinUi::Window(eventLoop)
    , Compositor(eventLoop)
    , m_password(nullptr)
    , m_key(nullptr)
    , m_label(nullptr)
    , m_warningLabel(nullptr)
    , m_busyIndicator(nullptr)
    , m_timer(0)
    , m_canShowError(false)
    , m_createdUI(false)
    , m_displayOn(true) // because minui unblanks screen on startup
    , m_checkTemporaryKey(true)
    , m_dbus(nullptr)
    , m_socket(nullptr)
    , m_watcher(false)
    , m_emergencyButton(nullptr)
    , m_emergencyBackground(nullptr)
    , m_emergencyLabel(nullptr)
    , m_call(Call(eventLoop))
    , m_speakerButton(nullptr)
{
}

void PinUi::createUI()
{
    if (m_createdUI)
        return;

    log_debug("create ui");
    m_createdUI = true;

    setBlankPreventWanted(true);

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
    m_busyIndicator->setY(m_label->y() + m_label->height() + MinUi::theme.paddingLarge);

    m_emergencyButton = new MinUi::IconButton("icon-lockscreen-emergency-call", this);
    m_emergencyButton->setX(m_label->x());
    m_emergencyButton->setY(m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
    MinUi::Palette palette;
    palette.normal = color_lightred;
    palette.selected = color_lightred;
    m_emergencyButton->setPalette(palette);
    m_emergencyButton->onActivated([this]() {
        setEmergencyMode(!m_emergencyMode);
    });

    m_key->onKeyPress([this](int code, char character) {
        if (code == ACCEPT_CODE) {
            if (m_emergencyMode) {
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
                m_timer = window()->eventLoop()->createTimer(16, [this]() {
                    window()->eventLoop()->cancelTimer(m_timer);
                    m_timer = 0;
                    disableAll();
                    startAskWatcher();
                    sendPassword(m_password->text());
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
        } else if (character && m_timer == 0) {
            if (m_warningLabel) {
                reset();
            }
            m_password->setText(m_password->text() + character);
        }
    });
}

void PinUi::setEmergencyMode(bool emergency)
{
    m_password->setText("");
    if (emergency) {
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

        if (!m_speakerButton) {
            m_speakerButton = new MinUi::IconButton("icon-m-speaker", this);
            m_speakerButton->centerBetween(*this, MinUi::Left, *this, MinUi::Right);;
            m_speakerButton->setY(m_label->y() + m_label->height() + m_speakerButton->height() + MinUi::theme.paddingLarge);
            m_speakerButton->setPalette(palette);

            m_speakerButton->onActivated([this]() {
                m_call.toggleSpeaker();
                MinUi::Palette palette = m_speakerButton->palette();
                if (m_call.speakerEnabled()) {
                    palette.normal = color_white;
                    palette.selected = color_white;
                    palette.pressed = color_red;
                } else {
                    palette.normal = color_red;
                    palette.selected = color_red;
                    palette.pressed = color_white;
                }
                m_speakerButton->setPalette(palette);
            });
        } else {
            m_speakerButton->setVisible(true);
        }

        m_emergencyMode = true;
    } else {
        m_emergencyBackground->setVisible(false);
        m_emergencyLabel->setVisible(false);
        m_label->setVisible(true);
        m_key->setAcceptVisible(false);
        m_key->setCancelVisible(false);
        MinUi::Palette palette;
        m_password->setPalette(m_palette);
        m_password->setEchoDelay(100); // Default number masking
        m_key->setPalette(m_palette);
        m_key->setAcceptText(nullptr);
        m_key->setAcceptVisible(false);
        m_emergencyButton->setVisible(true);
        m_speakerButton->setVisible(false);
        if (m_warningLabel) {
            delete m_warningLabel;
            m_warningLabel = nullptr;
        }

        m_emergencyMode = false;
    }
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
}

void PinUi::updateAcceptVisibility()
{
    if (!m_createdUI || m_emergencyMode)
        return;

    if (m_password->text().length() < DeviceLockSettings::instance()->minimumCodeLength()) {
        m_key->setAcceptVisible(false);
    } else {
        m_key->setAcceptVisible(true);
    }
}

int PinUi::execute(const char *socket)
{
    m_socket = socket;
    if (m_checkTemporaryKey && temporary_key_is_set()) {
        // Send temporary key
        m_timer = window()->eventLoop()->createTimer(16, [this]() {
            window()->eventLoop()->cancelTimer(m_timer);
            m_timer = 0;
            disableAll();
            startAskWatcher();
            sendPassword(TEMPORARY_KEY);
        });
    } else {
        // Not using temporary key, don't check it again
        m_checkTemporaryKey = false;
    }
    return window()->eventLoop()->execute();
}

void PinUi::exit(int value)
{
    if (m_timer) {
        eventLoop()->cancelTimer(m_timer);
        m_timer = 0;
    }
    eventLoop()->exit(value);
}

void PinUi::showError()
{
    if (!m_createdUI)
        return;

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

void PinUi::sendPassword(const std::string& password)
{
    char buf[DeviceLockSettings::instance()->maximumCodeLength() + 2];
    int sd, len;
    struct sockaddr_un name;

    if (password.length() > 0) {
        buf[0] = '+';
        strncpy(buf + 1, password.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        len = strlen(buf);
    } else {  // Assume cancelled
        buf[0] = '-';
        len = 1;
    }

    if ((sd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
        log_err("socket open failed");
        return;
    }

    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, m_socket, sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = '\0';

    if (connect(sd, (struct sockaddr *)&name, SUN_LEN(&name)) != 0) {
        log_err("socket connect failed");
        close(sd);
        return;
    }

    if (send(sd, buf, len, 0) < len) {
        log_err("socket send failed");
        close(sd);
        return;
    }

    close(sd);
}

void PinUi::startAskWatcher()
{
    if (!m_watcher) {
        int fd = inotify_init1(IN_NONBLOCK);
        if (fd < 0) {
            log_err("inotify init failed");
            return;
        }
        int askWatch = inotify_add_watch(fd, ask_dir, IN_MOVED_TO);
        if (askWatch < 0) {
            log_err("inotify watch add failed");
            return;
        }

        std::function<NotifierCallbackType> callback(askWatcher);
        eventLoop()->addNotifierCallback(fd, callback);

        m_watcher = true;
    }
}

bool PinUi::askWatcher(int descriptor, uint32_t events)
{
    (void)events;

    // Flush inotify events
    int available;
    if (!ioctl(descriptor, FIONREAD, &available)) {
        char buf[available];
        if (read(descriptor, buf, available) == -1) {
            // really don't care
        }
    }

    PinUi *instance = PinUi::instance();
    // New file moved to the ask directory, assume password failed
    if (instance->m_checkTemporaryKey) {
        // Passphrase is not temporary key, ask user for key
        instance->m_checkTemporaryKey = false;
        instance->enabledAll();
    } else {
        instance->showError();
    }
    // Timer to exit the mainloop
    instance->m_timer = instance->eventLoop()->createTimer(100, []() {
        PinUi::instance()->exit(0);
    });
    return true;
}

void PinUi::setEmergencyCallStatus(Call::Status status)
{
    if (m_warningLabel) {
        delete m_warningLabel;
        m_warningLabel = nullptr;
    }
    switch (status) {
        case Call::Status::Calling:
            m_warningLabel = createLabel(m_calling_emergency, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_key->setAcceptText(m_end_call);
            break;
        case Call::Status::Error:
            m_warningLabel = createLabel(m_emergency_call_failed, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_key->setAcceptText(m_start_call);
            break;
        case Call::Status::InvalidNumber:
            m_warningLabel = createLabel(m_invalid_emergency_number, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            break;
        case Call::Status::Ended:
            m_warningLabel = createLabel(m_emergency_call_ended, m_label->y() + m_label->height() + MinUi::theme.paddingLarge);
            m_key->setAcceptText(m_start_call);
            break;
        default:
            break;
    }
    if (m_warningLabel)
        m_warningLabel->setColor(color_white);
}

static inline bool temporary_key_is_set()
{
    struct stat buf;
    return stat(TEMPORARY_KEY_FILE, &buf) == 0;
}
