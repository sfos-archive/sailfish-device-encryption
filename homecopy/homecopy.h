/*
 * Copyright (c) 2021 Jolla Ltd.
 *
 * License: Proprietary
 */

#ifndef __COPY_HOME_H
#define __COPY_HOME_H

typedef enum _copy_state {
    NOT_COPYING,
    COPYING
} copy_state;

typedef enum _copy_result {
    COPY_SUCCESS,
    COPY_FAILED,
    COPY_PROCESS_ERROR
} copy_result;

copy_state get_copy_state();
void set_copy_done();
void copy_home(GMainLoop *main_loop, void (*emit_signal)(copy_result res));
void restore_home(GMainLoop *main_loop, void (*emit_signal)(copy_result res));
gboolean set_copy_location(gchar *path);

#endif // __COPY_HOME_H
