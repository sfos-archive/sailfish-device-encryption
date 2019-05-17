/*
 * Copyright (C) 2019 Jolla Ltd
 */

#ifndef UNLOCK_AGENT_LOGGING_H_
#define UNLOCK_AGENT_LOGGING_H_
#include <iostream>
#define log_err(ITEMS) do { std::cerr << "E:" << ITEMS << std::endl; } while (0)
#define log_warning(ITEMS) do { std::cerr << "W:" << ITEMS << std::endl; } while (0)
#if LOGGING_ENABLE_DEBUG
#define log_debug(ITEMS) do { std::cerr << "D:" << ITEMS << std::endl; } while (0)
#else
#define log_debug(ITEMS) do { } while (0)
#endif
#if LOGGING_ENABLE_PRIVATE
#define log_private(ITEMS) do { std::cerr << "P:" << ITEMS << std::endl; } while (0)
#else
#define log_private(ITEMS) do { } while (0)
#endif
#endif /* UNLOCK_AGENT_LOGGING_H_ */
