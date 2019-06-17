/* Copyright (c) 2019 Jolla Ltd. */

#include <dirent.h>
#include <errno.h>
#include <glib.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <iostream>
#include <sailfish-minui/eventloop.h>
#include "pin.h"
#include "touchinput.h"
#include "devicelocksettings.h"

using namespace Sailfish;

#define USECS(tp) (tp.tv_sec * 1000000L + tp.tv_nsec / 1000L)
#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)
#define FREE_LIST(l, c, i) for (i = 0; i < c; i++) free(l[i]); free(l);
#define STRCCAT(d, s, m) strncat(d, s, sizeof(d) - strlen(m) - 1); \
    d[sizeof(d) - 1] = '\0';
#define STRCCPY(d, s) strncpy(d, s, sizeof(d) - 1); \
    d[sizeof(d) - 1] = '\0';

typedef struct {
    char ask_file[108];
    int pid;
    char socket[108];
    int accept_cached;
    int echo;
    long not_after;
    char id[100];
    char message[100];
} ask_info_t;

char s_socket[108];

// Declarations
static inline ask_info_t *ask_info_new(char *ask_file);
static int is_ask_file(const struct dirent *ep);
static bool time_in_past(long time);
static inline bool ask_info_from_g_key_file(ask_info_t *ask_info, GKeyFile *key_file);
static inline ask_info_t*ask_parse(char*ask_file);
static int ask_scan();
static void ask_ui(void);
int main(int ac, char **av);

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

static int is_ask_file(const struct dirent *ep)
{
    if (ep->d_type != DT_REG)
        return 0;

    if (strncmp(ep->d_name, "ask.", 4) != 0)
        return 0;

    return 1;
}

static bool time_in_past(long time)
{
    struct timespec tp;

    if (time > 0 && clock_gettime(CLOCK_MONOTONIC, &tp) == 0 && time < USECS(tp))
        return true;

    return false;
}

static inline bool ask_info_from_g_key_file(ask_info_t *ask_info,
                                            GKeyFile *key_file)
{
    GError *error = NULL;
    gchar *str;

    ask_info->pid = g_key_file_get_integer(key_file, "Ask", "PID", &error);
    if (ask_info->pid == 0) {
        fprintf(stderr, "Warning: Error reading PID: %s\n", error->message);
        g_error_free(error);
    }

    str = g_key_file_get_string(key_file, "Ask", "Socket", &error);
    if (str == NULL) {
        fprintf(stderr, "Critical: Error reading Socket: %s\n",
                error->message);
        g_error_free(error);
        return false;
    }
    STRCCPY(ask_info->socket, str);
    g_free(str);

    ask_info->accept_cached = g_key_file_get_integer(key_file, "Ask",
                                                     "AcceptCached", &error);
    if (ask_info->accept_cached == 0 && error != NULL) {
        fprintf(stderr, "Warning: Error reading AcceptCached: %s\n",
                error->message);
        g_error_free(error);
    }

    ask_info->echo = g_key_file_get_integer(key_file, "Ask", "Echo", &error);
    if (ask_info->echo && error != NULL) {
        fprintf(stderr, "Warning: Error reading Echo: %s\n", error->message);
        g_error_free(error);
        return false;
    }

    ask_info->not_after = g_key_file_get_integer(key_file, "Ask", "NotAfter",
                                                 &error);
    if (ask_info->not_after && error != NULL) {
        fprintf(stderr, "Warning: Error reading NotAfter: %s\n",
                error->message);
        g_error_free(error);
    }

    str = g_key_file_get_string(key_file, "Ask", "Id", &error);
    if (str == NULL) {
        fprintf(stderr, "Warning: Error reading Id: %s\n",
                error->message);
        g_error_free(error);
    } else {
        STRCCPY(ask_info->id, str);
        g_free(str);
    }

    str = g_key_file_get_string(key_file, "Ask", "Message", &error);
    if (str == NULL) {
        fprintf(stderr, "Warning: Error reading Message: %s\n",
                error->message);
        g_error_free(error);
    } else {
        STRCCPY(ask_info->message, str);
        g_free(str);
    }

    return true;
}

static inline ask_info_t* ask_parse(char* ask_file)
{
    ask_info_t* ask_info = ask_info_new(ask_file);
    if (ask_info) {
        GError *error = NULL;
        GKeyFile *key_file = g_key_file_new();
        if (!g_key_file_load_from_file(key_file, ask_info->ask_file,
                                       G_KEY_FILE_NONE, &error)) {
            fprintf(stderr, "reading ask file failed: %s\n", error->message);
            g_error_free(error);
            g_key_file_unref(key_file);
            free(ask_info);
            return NULL;
        }

        if (!ask_info_from_g_key_file(ask_info, key_file)) {
            g_key_file_unref(key_file);
            free(ask_info);
            return NULL;
        }
        g_key_file_unref(key_file);

        if (time_in_past(ask_info->not_after)) {
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

static int ask_scan()
{
    ask_info_t *ask_info;
    int count, i, ret = 0;
    struct dirent **files;

    count = scandir(ask_dir, &files, is_ask_file, alphasort);
    if (count == -1)
        return 0;

    if (count < 1) {
        FREE_LIST(files, count, i);
        return 0;
    }

    for (i = 0; i < count; i++) {
        ask_info = ask_parse(files[i]->d_name);
        if (ask_info) {
            // Copy for later use
            strcpy(s_socket, ask_info->socket);
            free(ask_info);
            ret = 1;
            break;
        }
    }

    FREE_LIST(files, count, i);
    return ret;
}

static void ask_ui(void)
{
    // Start the UI
    PinUi* ui = PinUi::instance();
    // Execute does nothing if the UI is already running
    ui->execute(s_socket);
}

int main(int argc, char **argv)
{
    (void)argv;

    const int max_wait_seconds = 600;

    if (!touchinput_wait_for_device(max_wait_seconds))
        exit(EXIT_FAILURE);

    if (argc == 1) {
        /* No arguments -> default behavior */
        while (ask_scan())
            ask_ui();
    } else {
        /* Some arguments -> UI debugging */
        ask_ui();
    }
    return EXIT_SUCCESS;
}
