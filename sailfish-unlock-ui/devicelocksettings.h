/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#ifndef UNLOCK_AGENT_DEVICELOCK_SETTINGS
#define UNLOCK_AGENT_DEVICELOCK_SETTINGS

class DeviceLockSettings {
public:
    static DeviceLockSettings *instance();
    ~DeviceLockSettings();

    unsigned int minimumCodeLength() const;
    unsigned int maximumCodeLength() const;

private:
    DeviceLockSettings();

    unsigned int m_minimumCodeLength;
    unsigned int m_maximumCodeLength;
};

#endif
