/****************************************************************************************
** Copyright (c) 2019 - 2023 Jolla Ltd.
**
** All rights reserved.
**
** This file is part of Sailfish Device Encryption package.
**
** You may use this file under the terms of BSD license as follows:
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
**    list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
****************************************************************************************/

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
