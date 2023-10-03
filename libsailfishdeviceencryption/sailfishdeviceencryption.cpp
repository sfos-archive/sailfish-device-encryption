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

#include "sailfishdeviceencryption.h"

#include <QLoggingCategory>

#define homeLuksContainer "/dev/sailfish/home"

namespace Sailfish
{
namespace DeviceEncryption {

Q_LOGGING_CATEGORY(encryption, "org.sailfishos.device.encryption", QtWarningMsg)

crypt_device *getCryptDeviceForHome()
{
    struct crypt_device *cd;
    int err = crypt_init(&cd, homeLuksContainer);
    if (err < 0) {
        qCDebug(encryption, "Could not initialize crypt_device");
        return nullptr;
    }

    err = crypt_load(cd, NULL, NULL);
    if (err < 0) {
        qCDebug(encryption, "Could not load LUKS parameters for crypt_device");
        crypt_free(cd);
        return nullptr;
    }

    return cd;
}

bool isHomeEncrypted()
{
    struct crypt_device *cd = getCryptDeviceForHome();

    if (!cd) {
        return false;
    }

    crypt_free(cd);
    return true;
}

}
}
