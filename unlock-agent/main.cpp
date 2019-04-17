/* Copyright (c) 2019 Jolla Ltd. */

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/un.h>

#include <iostream>

#include <libudev.h>

#include <sailfish-minui/eventloop.h>

#include "ini.h"
#include "pin.h"

#define USECS(tp) (tp.tv_sec * 1000000L + tp.tv_nsec / 1000L)
#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)
#define FREE_LIST(l, c, i) for (i = 0; i < c; i++) free(l[i]); free(l);
#define STRCCAT(d, s, m) strncat(d, s, sizeof(d) - strlen(m) - 1); \
    d[sizeof(d) - 1] = '\0';
#define STRCCPY(d, s) strncpy(d, s, sizeof(d) - 1); \
    d[sizeof(d) - 1] = '\0';

const char *ask_dir = "/run/systemd/ask-password/";
const char* TEMP_DM_NAME = "temporary-cryptsetup-";

typedef struct {
    char ask_file[108];
    int pid;
    char socket[108];
    int accept_cached;
    int echo;
    long not_after;
    char id[10];
    char message[100];
} ask_info_t;

typedef int (*hide_callback_t)(void *cb_data);

char s_socket[108];

// Declarations
int ask_scan();

static inline ask_info_t * ask_info_new(char *ask_file)
{
    ask_info_t *ask_info = (ask_info_t *)malloc(sizeof(ask_info_t));
    if (ask_info == NULL)
        return NULL;
    STRCCPY(ask_info->ask_file, ask_dir);
    STRCCAT(ask_info->ask_file, ask_file, ask_dir);
    ask_info->pid = -1;
    ask_info->socket[0] = '\0';
    ask_info->accept_cached = -1;
    ask_info->echo = 0;
    ask_info->not_after = -1;
    ask_info->id[0] = '\0';
    ask_info->message[0] = '\0';
    return ask_info;
}

int is_ask_file(const struct dirent *ep)
{
    if (ep->d_type != DT_REG)
        return 0;

    if (strncmp(ep->d_name, "ask.", 4) != 0)
        return 0;

    return 1;
}

int time_in_past(long time)
{
    struct timespec tp;

    if (time > 0 && clock_gettime(CLOCK_MONOTONIC, &tp) == 0 && time < USECS(tp))
        return 1;

    return 0;
}

int handle_ini_line(void *user, const char *section, const char *name,
        const char *value)
{
    ask_info_t *ask_info = (ask_info_t *)user;

    if (MATCH("Ask", "PID")) {
        ask_info->pid = atoi(value);

    } else if (MATCH("Ask", "Socket")) {
        STRCCPY(ask_info->socket, value);

    } else if (MATCH("Ask", "AcceptCached")) {
        ask_info->accept_cached = atoi(value);

    } else if (MATCH("Ask", "Echo")) {
        ask_info->echo = atoi(value);

    } else if (MATCH("Ask", "NotAfter")) {
        ask_info->not_after = atol(value);

    } else if (MATCH("Ask", "Id")) {
        STRCCPY(ask_info->id, value);

    } else if (MATCH("Ask", "Message")) {
        STRCCPY(ask_info->message, value);
    }

    return 1;
}

// TODO: malloc buf if password is not fixed maximum size (now 30 char)
static inline int send_password(const char *path, const char *password,
                                int len)
{
    char buf[32];
    int sd;
    struct sockaddr_un name;

    if (len >= 0) {
        buf[0] = '+';
        if (password != NULL) {  // Just to be sure
            strncpy(buf + 1, password, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            len = strlen(buf);
        } else
            len = 1;
    } else {  // Assume cancelled
        buf[0] = '-';
        len = 1;
    }

    if ((sd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
        return -errno;

    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, path, sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = '\0';

    if (connect(sd, (struct sockaddr *)&name, SUN_LEN(&name)) != 0)
        return -errno;

    if (send(sd, buf, len, 0) < 0)
        return -errno;

    return len;
}

void pin(const std::string& code)
{
    if (send_password(s_socket, code.c_str(), code.length()) < code.length()) {
        // TODO: password send failed
        fprintf(stderr, "send_password failed\n");
    }

    // Wait for either the luks device to appear or new ask file
    struct udev* udev = udev_new();
    struct udev_monitor* mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "block", NULL);
    udev_monitor_enable_receiving(mon);
    int ufd = udev_monitor_get_fd(mon);

    int fd = inotify_init1(IN_NONBLOCK);

    if (fd > 0 && ufd > 0) {
        int askWatch = inotify_add_watch(fd, ask_dir, IN_CREATE);
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        FD_SET(ufd, &rfds);
        bool exitMain = false;
        bool notifyOk = false;
        while (!notifyOk) {
            if (select(std::max(fd, ufd) + 1, &rfds, NULL, NULL, NULL) > 0) {
                if (FD_ISSET(fd, &rfds)) {
                    // New ask file
                    // Short wait otherwise the ask file is not there yet
                    sleep(1);
                    if (!ask_scan())
                        notifyOk = true;
                } else if (FD_ISSET(ufd, &rfds)) {
                    // Change in udev block devices
                    struct udev_device* dev = udev_monitor_receive_device(mon);
                    if (udev_device_get_devnode(dev)) {
                        const char* action = udev_device_get_action(dev);
                        // Must be change
                        if (!strcmp(action, "change")) {
                            const char* name = udev_device_get_property_value(dev, "DM_NAME");
                            if (name && strncmp(name, TEMP_DM_NAME, strlen(TEMP_DM_NAME))) {
                                // DM_NAME without temp name means the unlocked device appeared
                                notifyOk = true;
                                exitMain = true;
                            }
                        }
                    }
                    udev_device_unref(dev);
                }
            }
        }
        close(fd);
        close(ufd);
        // App exit if udev says the device appeared
        if (exitMain) PinUi::instance()->exit(0);
    }
}

// TODO: May be rewritten to use the event loop system of minui
// Returns 1 if dialog must be hidden, returns 0 otherwise
int hide_dialog(void *cb_data)
{
    ask_info_t *ask_info = (ask_info_t *) cb_data;
    struct stat sb;

    if (time_in_past(ask_info->not_after) == 1)
        return 1;

    if (stat(ask_info->ask_file, &sb) == -1)
        return 1;

    return 0;
}

static inline ask_info_t* ask_parse(char* ask_file)
{
    ask_info_t* ask_info = ask_info_new(ask_file);
    if (ask_info) {
        if (ini_parse(ask_info->ask_file, handle_ini_line,
                      ask_info) < 0) {
            free(ask_info);
            return NULL;
        }
        if (time_in_past(ask_info->not_after) == 1) {
            free(ask_info);
            return NULL;
        }
        if (kill(ask_info->pid, 0) == ESRCH) {
            free(ask_info);
            return NULL;
        }
    }
    return ask_info;
}

int ask_scan()
{
    ask_info_t *ask_info;
    int count, i, ret = 0;
    struct dirent **files;

    count = scandir(ask_dir, &files, is_ask_file, alphasort);
    if (count == -1)
        return 1;

    if (count < 1) {
        FREE_LIST(files, count, i);
        return 8;
    }

    for (i = 0; i < count; i++) {
        ask_info = ask_parse(files[i]->d_name);
        if (!ask_info) {
            ret = -1;
            break;
        }

        // Save the socket for later use
        strcpy(s_socket, ask_info->socket);
        // Start the UI
        PinUi* ui = PinUi::instance();
        ui->reset();
        // Execute does nothing if the UI is already running
        int rv = ui->execute(pin);
        if (rv != -1) ret = rv;

        free(ask_info);
    }

    FREE_LIST(files, count, i);

    return ret;
}

int main(void)
{
    // Check ask files and start the UI if needed
    return ask_scan();
}
