/* Copyright (c) 2019 Jolla Ltd. */

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <iostream>

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

#define TEMPORARY_PASSWORD "guitar"

const char *ask_dir = "/run/systemd/ask-password/";

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
static inline int send_password(ask_info_t *ask_info, const char *password,
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
    strncpy(name.sun_path, ask_info->socket, sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = '\0';

    if (connect(sd, (struct sockaddr *)&name, SUN_LEN(&name)) != 0)
        return -errno;

    if (send(sd, buf, len, 0) < 0)
        return -errno;

    return len;
}

// TODO: Needs to be re-engineered to work with minui event loop stuff,
// but this works for now and represents how I thought it'd work
int get_password(const char *message, int echo, hide_callback_t cb,
            void *cb_data, char **password)
{
    // Temporary implementation, to be replaced
    // message is to be printed, echo tell whether to show password or not
    if (cb(cb_data) == 1)  // Check callback on every "iteration"
        return -1;  // Cancelled

    MinUi::EventLoop eventLoop;
    PinUi pinUi(&eventLoop);
    eventLoop.execute();
    std::string code = pinUi.code();
    std::cout << "code:" << code << std::endl;

    if (code.empty()) *password = strdup(TEMPORARY_PASSWORD);
    else *password = strdup(code.c_str());
    if (*password == NULL)
        return -ENOMEM;

    return strlen(*password);  // Length of the password given
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

int main(void)
{
    ask_info_t *ask_info;
    int count, i, ret;
    struct dirent **files;
    char *password;

    count = scandir(ask_dir, &files, is_ask_file, alphasort);
    if (count == -1)
        return 1;

    if (count < 1) {
        FREE_LIST(files, count, i);
        return 8;
    }

    for (i = 0; i < count; i++) {
        ret = 0;
        password = NULL;
        ask_info = ask_info_new(files[i]->d_name);
        if (ask_info == NULL) {
            ret = -ENOMEM;
            break;
        }

        if (ini_parse(ask_info->ask_file, handle_ini_line,
                      ask_info) < 0)
            goto next;

        if (time_in_past(ask_info->not_after) == 1)
            goto next;

        if (kill(ask_info->pid, 0) == ESRCH)
            goto next;

        ret = get_password(ask_info->message, ask_info->echo,
                           &hide_dialog, ask_info, &password);
        ret = send_password(ask_info, password, ret);

        free(password);
next:
        free(ask_info);

        if (ret < 0)
            break;
    }

    FREE_LIST(files, count, i);

    if (ret < 0)
        return 1;

    return 0;
}
