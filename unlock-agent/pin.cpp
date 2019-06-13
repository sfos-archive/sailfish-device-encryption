/*
 * Copyright (C) 2019 Jolla Ltd
 */

#include "pin.h"
#include "logging.h"
#include "devicelocksettings.h"
#include <sailfish-minui-dbus/eventloop.h>

#include <unistd.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <gudev/gudev.h>

#define ACCEPT_CODE 28
#define UDISKS_DBUS_NAME "org.freedesktop.UDisks2"
#define UDISKS_UUID_PROPERTY "DM_UUID"
#define TEMPORARY_KEY_FILE \
    "/var/lib/sailfish-device-encryption/temporary-encryption-key"
#define TEMPORARY_KEY "00000"

static inline bool temporary_key_is_set();

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
    , m_timer(0)
    , m_canShowError(false)
    , m_createdUI(false)
    , m_displayOn(true) // because minui unblanks screen on startup
    , m_checkTemporaryKey(true)
    , m_dbus(nullptr)
    , m_socket(nullptr)
{
    watchForDBusChanges();
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
                window()->eventLoop()->cancelTimer(m_timer);
                m_timer = 0;
                disableAll();
                startAskWatcher();
                sendPassword(m_password->text());
            });
        } else if (character && m_timer == 0) {
            if (m_warningLabel) {
                reset();
            }
            m_password->setText(m_password->text() + character);
        }
    });
}

void PinUi::watchForDBusChanges()
{
    // Get notified of DBus changes
    m_dbus = new MinDBus::Object(MinDBus::systemBus(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    m_dbus->connect<const char*, const char*, const char*>("NameOwnerChanged", [](const char* name, const char*, const char*) {
        if (!strcmp(name, UDISKS_DBUS_NAME)) {
            // UDisks2 change, check block devices
            const gchar* subsystems[] = {"block", NULL};
            GUdevClient* client = g_udev_client_new(subsystems);
            if (client) {
                bool cryptFound = false;
                GList* devices = g_udev_client_query_by_subsystem (client, subsystems[0]);
                for (GList* list = devices; list != NULL; list = list->next) {
                    GUdevDevice* device = (GUdevDevice*) list->data;
                    if (g_udev_device_has_property(device, UDISKS_UUID_PROPERTY)) {
                        if (!strncmp(g_udev_device_get_property(device, UDISKS_UUID_PROPERTY), "CRYPT-", 6)) {
                            // Crypted device, assume password ok
                            cryptFound = true;
                            break;
                        }
                    }
                }
                g_list_free_full(devices, g_object_unref);
                g_object_unref(client);
                if (cryptFound) {
                    PinUi::instance()->exit(0);
                }
            }
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
}

bool PinUi::askWatcher(int descriptor, uint32_t events)
{
    (void)events;
    close(descriptor);
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

static inline bool temporary_key_is_set()
{
    struct stat buf;
    return stat(TEMPORARY_KEY_FILE, &buf) == 0;
}
