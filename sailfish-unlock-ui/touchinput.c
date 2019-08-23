/*
 * Copyright (C) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#include "touchinput.h"
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <glob.h>
#include <fcntl.h>
#include <time.h>

/* ========================================================================= *
 * Config
 * ========================================================================= */

/** Waiting for OTG mouse can be useful for testing/debugging purposes */
#define WAIT_FOR_MOUSE_DEVICE 0

/* ========================================================================= *
 * Constants
 * ========================================================================= */

#define INPUTDEV_DIRECTORY "/dev/input"
#define INPUTDEV_PREFIX    "event"
#define INPUTDEV_PATTERN   INPUTDEV_DIRECTORY "/" INPUTDEV_PREFIX "*"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * UTILITY
 * ------------------------------------------------------------------------- */

static int64_t get_clock_tick(clockid_t id);
static int64_t get_boot_tick (void);

/* ------------------------------------------------------------------------- *
 * TOUCHINPUT
 * ------------------------------------------------------------------------- */

static bool touchinput_is_touch_device      (const char *path);
static bool touchinput_have_touch_device    (void);
static bool touchinput_handle_inotify_events(int fd, bool *have_touch);
bool        touchinput_wait_for_device      (int max_wait_seconds);

/* ========================================================================= *
 * Logging
 * ========================================================================= */
#define log_err(FMT,ARGS...) fprintf(stderr, "E: " FMT "\n", ##ARGS)
#if LOGGING_ENABLE_DEBUG
#define log_debug(FMT,ARGS...) fprintf(stderr, "D: " FMT "\n", ##ARGS)
#else
#define log_debug(FMT,ARGS...) do { } while (0)
#endif

/* ========================================================================= *
 * Utility
 * ========================================================================= */

#define BMAP_SIZE(bc) (((bc)+LONG_BIT-1)/LONG_BIT)

static inline bool bmap_test(unsigned long *bmap, unsigned bi)
{
    unsigned i = bi / LONG_BIT;
    unsigned long m = 1ul << (bi % LONG_BIT);
    return (bmap[i] & m) != 0;
}

static inline void *lea(const void *addr, ssize_t offs)
{
    return offs + (char *)addr;
}

static int64_t get_clock_tick(clockid_t id)
{
    int64_t res = 0;

    struct timespec ts;

    if (clock_gettime(id, &ts) == 0) {
        res = ts.tv_sec;
        res *= 1000;
        res += ts.tv_nsec / 1000000;
    }

    return res;
}

static int64_t get_boot_tick(void)
{
    return get_clock_tick(CLOCK_BOOTTIME);
}

static bool
touchinput_is_touch_device(const char *path)
{
    bool is_touchdev = false;
    int fd = -1;

    log_debug("%s: probing ...", path);

    if ((fd = open(path, O_RDONLY)) == -1) {
        log_err("%s: could not open: %m", path);
        goto EXIT;
    }

    unsigned long types[BMAP_SIZE(EV_CNT)];
    unsigned long codes[BMAP_SIZE(KEY_CNT)];

    if (ioctl(fd, EVIOCGBIT(0, EV_CNT), memset(types, 0, sizeof types)) == -1) {
        log_err("%s: failed to probe event types: %m", path);
        goto EXIT;
    }

#if WAIT_FOR_MOUSE_DEVICE
    /* Debugging: Wait for OTG mouse
     */
    if (!bmap_test(types, EV_KEY))
        goto EXIT;

    if (ioctl(fd, EVIOCGBIT(EV_KEY, KEY_CNT), memset(codes, 0, sizeof codes)) == -1) {
        log_err("%s: failed to probe EV_KEY event codes: %m", path);
        goto EXIT;
    }
    if (bmap_test(codes, BTN_FORWARD)) {
        log_debug("%s: is a \"touch input device\" (aka mouse)", path);
        is_touchdev = true;
    }
#else
    /* For real: Wait for touchscreen
     */
    if (!bmap_test(types, EV_ABS)) {
        /* No EV_ABS events -> not a multitouch input device */
        goto EXIT;
    }

    if (ioctl(fd, EVIOCGBIT(EV_ABS, ABS_CNT), memset(codes, 0, sizeof codes)) == -1) {
        /* No EV_ABS info -> further probing is useless */
        log_err("%s: failed to probe EV_ABS event codes: %m", path);
        goto EXIT;
    }

    if (bmap_test(codes, ABS_MT_POSITION_X)) {
        log_debug("%s: is a touch input device", path);
        is_touchdev = true;
    }
#endif

EXIT:
    if (fd != -1)
        close(fd);

    return is_touchdev;
}

static bool
touchinput_have_touch_device(void)
{
    bool have_touchdev = false;

    glob_t gl = { };
    if (glob(INPUTDEV_PATTERN, 0, 0, &gl) == 0) {
        for (size_t i = 0; i < gl.gl_pathc; ++i) {
            if (touchinput_is_touch_device(gl.gl_pathv[i])) {
                have_touchdev = true;
                break;
            }
        }
    }
    globfree(&gl);

    return have_touchdev;
}

static bool
touchinput_handle_inotify_events(int fd, bool *have_touch)
{
    static const char pfix[] = INPUTDEV_PREFIX;

    bool ack = true;

    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

    log_debug("inotify wakeup");

    for (;;) {
        int rc = read(fd, buf, sizeof buf);
        if (rc == -1) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN)
                break;
            if (errno == EWOULDBLOCK)
                break;
            log_err("failed to read inotify events: %m");
            ack = false;
            break;
        }

        log_debug("inotify data: %d bytes", rc);

        const void *now = lea(buf, 0);
        const void *end = lea(buf, rc);

        for (const void *zen = end; now < end; now = zen) {
            const struct inotify_event *eve = now;

            if (lea(eve, sizeof *eve) > end) {
                log_err("broken inotify event");
                break;
            }

            zen = lea(eve, sizeof *eve + eve->len);

            if (zen > end) {
                log_err("broken inotify event data");
                break;
            }

            log_debug("inotify event: mask=0x%x", eve->mask);

            if (!(eve->mask & IN_CREATE))
                continue;

            const char *name = eve->len ? eve->name : 0;
            if (!name)
                continue;

            if (strncmp(name, pfix, sizeof pfix - 1))
                continue;

            char path[PATH_MAX];
            snprintf(path, sizeof path, "%s/%s", INPUTDEV_DIRECTORY, name);
            if (touchinput_is_touch_device(path))
                *have_touch = true;
        }
    }
    return ack;
}

bool
touchinput_wait_for_device(int max_wait_seconds)
{
    bool have_touch = false;

    int fd = -1;
    int wd = -1;

    /* Timeout is: From entry to this function */
    int64_t tmo = get_boot_tick() + max_wait_seconds * 1000LL;

    /* 1) Setup inotify watch for input device directory
     * 2) Check currently available input devices
     * 3) Wait and process inotify events
     *
     * -> We do not leave a gap between probing existing input
     *    devices and getting notified about new ones.
     */
    if ((fd = inotify_init1(IN_NONBLOCK)) == -1) {
        log_err("inotify_init1() failed: %m");
        goto EXIT;
    }

    if ((wd = inotify_add_watch(fd, INPUTDEV_DIRECTORY, IN_CREATE)) == -1) {
        log_err("inotify_add_watch() failed: %m");
        goto EXIT;
    }

    /* Probe existing input devices */
    if ((have_touch = touchinput_have_touch_device()))
        goto EXIT;

    log_debug("waiting for touch device to show up");

    /* Process input device added events */
    while (!have_touch) {
        struct pollfd pfd = {
            .fd     = fd,
            .events = POLLIN,
        };

        /* Poll once even if we did run out of time during probing */
        int64_t now = get_boot_tick();
        int timeout = (now < tmo) ? (int)(tmo - now) : 0;
        int rc = poll(&pfd, 1, timeout);

        if (rc == 0) {
            log_err("timeout");
            break;
        }

        if (rc == -1) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN)
                continue;
            log_err("poll() failed: %m");
            break;
        }

        if (pfd.revents & POLLIN) {
            if (!touchinput_handle_inotify_events(fd, &have_touch))
                break;
        }
    }

EXIT:
    if (fd != -1) {
        if (wd != -1)
            inotify_rm_watch(fd, wd);
        close(fd);
    }

    return have_touch;
}
