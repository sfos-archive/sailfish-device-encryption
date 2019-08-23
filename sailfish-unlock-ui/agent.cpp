/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#include "agent.h"
#include "compositor.h"
#include "devicelocksettings.h"
#include "logging.h"
#include "pin.h"
#include <dirent.h>
#include <glib.h>
#include <sailfish-minui-dbus/eventloop.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

using namespace Sailfish;

#define LIPSTICK_SERVICE "org.nemomobile.lipstick"
#define TEMPORARY_PASSPHRASE_FILE \
    "/var/lib/sailfish-device-encryption/temporary-encryption-key"
#define TEMPORARY_PASSPHRASE "00000"

#define USECS(tp) (tp.tv_sec * 1000000L + tp.tv_nsec / 1000L)
#define FREE_LIST(l, c) for (int i = 0; i < c; i++) free(l[i]); free(l);

static const std::string askDir = "/run/systemd/ask-password/";

Agent::Agent()
    : Agent(new MinUi::DBus::EventLoop())
{
}

Agent::Agent(MinUi::DBus::EventLoop *eventLoop)
    : Compositor(eventLoop)
    , m_eventLoop(eventLoop)
    , m_ui(nullptr)
    , m_uiAllowed(false)
    , m_askFile("")
    , m_socket("")
    , m_pid(0)
    , m_notAfter(0)
{
}

Agent::~Agent()
{
    if (m_ui)
        delete m_ui;
}

bool Agent::checkIfAgentCanRun()
{
    bool ack = false;
    DBusError err = DBUS_ERROR_INIT;
    DBusConnection *systemBus = nullptr;
    if (!(systemBus = dbus_bus_get(DBUS_BUS_SYSTEM, &err))) {
        log_err("system bus connect failed: " << err.name << ": " <<  err.message);
    } else {
        if (dbus_bus_name_has_owner(systemBus, LIPSTICK_SERVICE, &err)) {
            log_err("lipstick is running, sailfish-unlock-ui ought not to be");
        } else if (dbus_error_is_set(&err) && strcmp(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER)) {
            log_err("failed to query lipstick availability: " << err.name << ": " << err.message);
        } else {
            ack = true;
        }
        dbus_connection_unref(systemBus);
    }
    dbus_error_free(&err);
    return ack;
}

int Agent::execute(bool uiDebugging)
{
    m_uiDebugging = uiDebugging;
    if (!m_uiDebugging) {
        findAskFile();
        startWatchingForAskFiles();

        // Handle temporary passphrase early
        if (checkForTemporaryPassphrase())
            return EXIT_SUCCESS;
    }

    // Now all "before UI" tasks are done and showing UI is allowed
    m_uiAllowed = true;
    tryToCreateUI();
    m_eventLoop->execute();

    return EXIT_SUCCESS;
}

void Agent::startWatchingForAskFiles()
{
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        log_err("inotify init failed");
        return;
    }
    int askWatch = inotify_add_watch(fd, askDir.c_str(), IN_MOVED_TO);
    if (askWatch < 0) {
        log_err("inotify watch add failed");
        return;
    }

    auto notifierCallback = std::bind(&Agent::handleNewAskFile, this, std::placeholders::_1, std::placeholders::_2);
    m_eventLoop->addNotifierCallback(fd, notifierCallback);
}

bool Agent::handleNewAskFile(int descriptor, uint32_t events)
{
    (void)events;
    log_debug("new ask file");

    // Flush inotify events
    int available;
    if (!ioctl(descriptor, FIONREAD, &available)) {
        char buf[available];
        if (read(descriptor, buf, available) == -1) {
            // really don't care
        }
    }

    if (!findAskFile())
        return true;

    // If UI exists inform user, otherwise quit mainloop to continue
    if (m_ui)
        m_ui->newAskFile();
    else
        m_eventLoop->exit(0);

    return true;
}

bool Agent::checkForTemporaryPassphrase()
{
    if (temporaryPassphraseIsSet() && !m_askFile.empty()) {
        sendPassword(TEMPORARY_PASSPHRASE);
        // Wait until /home is mounted or new ask file appears
        m_eventLoop->execute();
        // Return if temporary passphrase was correct
        return targetUnitActive();
    }
    return false;
}

bool Agent::temporaryPassphraseIsSet()
{
    struct stat buf;
    return stat(TEMPORARY_PASSPHRASE_FILE, &buf) == 0;
}

bool Agent::findAskFile()
{
    struct dirent **files;
    int count = scandir(askDir.c_str(), &files, isAskFile, alphasort);
    if (count == -1)
        return false;

    if (count < 1) {
        FREE_LIST(files, count);
        return false;
    }

    bool ret = false;
    for (int i = 0; i < count; i++) {
        if (parseAskFile(files[i]->d_name)) {
            log_debug("found ask file " << m_askFile);
            ret = true;
            break;
        }
    }

    FREE_LIST(files, count);
    return ret;
}

int Agent::isAskFile(const struct dirent *ep)
{
    if (ep->d_type != DT_REG)
        return 0;

    if (strncmp(ep->d_name, "ask.", 4) != 0)
        return 0;

    return 1;
}

bool Agent::parseAskFile(const char *askFile)
{
    m_askFile.assign("");

    GError *error = NULL;
    GKeyFile *keyFile = g_key_file_new();
    if (!g_key_file_load_from_file(keyFile, (askDir + askFile).c_str(), G_KEY_FILE_NONE, &error)) {
        log_err("reading ask file failed: " << error->message);
        g_error_free(error);
        g_key_file_unref(keyFile);
        return false;
    }

    if (!parseGKeyFile(keyFile)) {
        log_err("parsing ask file failed");
        g_key_file_unref(keyFile);
        return false;
    }
    g_key_file_unref(keyFile);

    if (m_notAfter > 0 && !timeInPast(m_notAfter)) {
        log_warning("ask file time in past");
        return false;
    }

    if (kill(m_pid, 0) == ESRCH) {
        log_warning("pid does not exist");
        return false;
    }

    m_askFile.assign(askFile);
    return true;
}

bool Agent::parseGKeyFile(GKeyFile *keyFile)
{
    GError *error = NULL;

    m_pid = g_key_file_get_integer(keyFile, "Ask", "PID", &error);
    if (m_pid == 0) {
        log_warning("Error reading PID: " << error->message);
        g_error_free(error);
        error = NULL;
    }

    gchar *str = g_key_file_get_string(keyFile, "Ask", "Socket", &error);
    if (str == NULL) {
        log_err("Error reading Socket: " << error->message);
        g_error_free(error);
        return false;
    }
    m_socket.assign(str);
    g_free(str);

    m_notAfter = g_key_file_get_integer(keyFile, "Ask", "NotAfter", &error);
    if (m_notAfter && error != NULL) {
        log_warning("Error reading NotAfter: " << error->message);
        g_error_free(error);
    }

    return true;
}

bool Agent::timeInPast(long time)
{
    struct timespec tp;

    if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0 && time < USECS(tp))
        return true;

    return false;
}

bool Agent::sendPassword(const std::string& password)
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
        return false;
    }

    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, m_socket.c_str(), sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = '\0';

    if (connect(sd, (struct sockaddr *)&name, SUN_LEN(&name)) != 0) {
        log_err("socket connect failed");
        close(sd);
        return false;
    }

    if (send(sd, buf, len, 0) < len) {
        log_err("socket send failed");
        close(sd);
        return false;
    }

    close(sd);
    return true;
}

void Agent::tryToCreateUI()
{
    // Create UI if it is not created and creating it is allowed
    if (!m_ui && m_uiAllowed && updatesEnabled()) {
        // Start the UI
        m_ui = new PinUi(this, m_eventLoop);
        if (m_uiDebugging || !m_askFile.empty())
            m_ui->newAskFile();
    }
}

void Agent::displayStateChanged()
{
    if (m_ui)
        m_ui->displayStateChanged();
}

void Agent::chargerStateChanged()
{
    if (m_ui)
        m_ui->chargerStateChanged();
}

void Agent::batteryStatusChanged()
{
    if (m_ui)
        m_ui->batteryStatusChanged();
}

void Agent::batteryLevelChanged()
{
    if (m_ui)
        m_ui->batteryLevelChanged();
}

void Agent::updatesEnabledChanged()
{
    tryToCreateUI();

    if (m_ui)
        m_ui->updatesEnabledChanged();
}

void Agent::dsmeStateChanged()
{
    if (m_ui)
        m_ui->dsmeStateChanged();
}

void Agent::targetUnitActiveChanged()
{
    if (m_ui)
        m_ui->targetUnitActiveChanged();
}
