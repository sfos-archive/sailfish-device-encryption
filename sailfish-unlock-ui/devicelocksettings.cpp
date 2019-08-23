/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

#include "devicelocksettings.h"

#include <glib.h>

#define DEVICELOCK_SETTINGS "/usr/share/lipstick/devicelock/devicelock_settings.conf"
// The same default values that we have in the device lock code side.
// The security code can be shorter or longer than these defaults.
#define MIN_CODE_LEN 5
#define MAX_CODE_LEN 42

static DeviceLockSettings *s_instance = nullptr;

DeviceLockSettings::DeviceLockSettings()
    : m_minimumCodeLength(MIN_CODE_LEN)
    , m_maximumCodeLength(MAX_CODE_LEN)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) key_file = g_key_file_new ();

    if (g_key_file_load_from_file(key_file, DEVICELOCK_SETTINGS,
                                   G_KEY_FILE_NONE, &error) && !error) {
        m_minimumCodeLength = g_key_file_get_integer(key_file, "desktop", "nemo\\devicelock\\code_min_length", &error);
        if (error && (!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
                      !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))) {
            m_minimumCodeLength = MIN_CODE_LEN;
        }

        m_maximumCodeLength = g_key_file_get_integer(key_file, "desktop", "nemo\\devicelock\\code_max_length", &error);
        if (error && (!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
                      !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))) {
            m_maximumCodeLength = MAX_CODE_LEN;
        }
    }
}

DeviceLockSettings *DeviceLockSettings::instance()
{
    if (!s_instance) {
        s_instance = new DeviceLockSettings;
    }

    return s_instance;
}

DeviceLockSettings::~DeviceLockSettings()
{

}

unsigned int DeviceLockSettings::minimumCodeLength() const
{
    return m_minimumCodeLength;
}

unsigned int DeviceLockSettings::maximumCodeLength() const
{
    return m_maximumCodeLength;
}
