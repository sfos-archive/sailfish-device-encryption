/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#ifndef UNLOCK_AGENT_TOUCHINPUT_H_
#define UNLOCK_AGENT_TOUCHINPUT_H_
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool touchinput_wait_for_device(int max_wait_seconds);

#ifdef __cplusplus
};
#endif

#endif /* UNLOCK_AGENT_TOUCHINPUT_H_ */
