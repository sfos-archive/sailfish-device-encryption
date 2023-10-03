/****************************************************************************************
** Copyright (c) 2019 Open Mobile Platform LLC.
** Copyright (c) 2023 Jolla Ltd.
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

import QtQml 2.2
import Sailfish.Encryption 1.0

EncryptionService {
    id: encryptionService

    property bool encrypting: false

    function checkAndEncrypt() {
        if (encryptionWanted && available && !encrypting) {
            encrypt()
            encrypting = true
        }
    }

    onAvailableChanged: checkAndEncrypt()
    onEncryptionWantedChanged: checkAndEncrypt()

    onEncryptionStatusChanged: {
        if (encryptionStatus === EncryptionStatus.Encrypted || encryptionStatus === EncryptionStatus.Error) {
            finalize()
            console.log("Encryption finished, quit.")
            Qt.quit()
        }
    }

    Component.onCompleted: {
        if (!encryptionWanted) {
            console.log("Encryption is not wanted, quit.")
            quitTimer.start()
        } else {
            checkAndEncrypt()
        }
    }

    property Timer quitTimer: Timer {
        id: quitTimer

        interval: 0
        onTriggered: Qt.quit()
    }
}
