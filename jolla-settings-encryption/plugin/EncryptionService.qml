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

import QtQuick 2.0
import Nemo.DBus 2.0
import Nemo.FileManager 1.0
import org.nemomobile.configuration 1.0
import Sailfish.Encryption 1.0

DBusInterface {
    id: encryptionService

    bus: DBus.SystemBus
    service: "org.sailfishos.EncryptionService"
    path: "/org/sailfishos/EncryptionService"
    iface: "org.sailfishos.EncryptionService"
    signalsEnabled: true
    // Prevents automatic introspection but simplifies the code otherwise
    watchServiceStatus: true

    property string errorString
    property string errorMessage
    property int encryptionStatus
    property bool serviceSeen
    readonly property bool encryptionWanted: encryptHome.exists && (status !== DBusInterface.Unavailable || serviceSeen)
    readonly property bool available: encryptHome.exists && (status === DBusInterface.Available || serviceSeen)
    readonly property bool busy: encryptionWanted && encryptionStatus == EncryptionStatus.Busy

    onStatusChanged: if (status === DBusInterface.Available) serviceSeen = true

    // DBusInterface is a QObject so no child items
    property FileWatcher encryptHome: FileWatcher {
        id: encryptHome
        fileName: "/var/lib/sailfish-device-encryption/encrypt-home"
    }

    // This introspects the interface. Thus, starting the dbus service.
    readonly property DBusInterface introspectAtStart: DBusInterface {
        bus: DBus.SystemBus
        service: encryptionService.service
        path: encryptionService.path
        iface: "org.freedesktop.DBus.Introspectable"
        Component.onCompleted: call("Introspect")
    }

    onAvailableChanged: {
        // Move to busy state right after service is available. So that
        // user do not see text change from Idle to Busy (encryption is started
        // when we hit the PleaseWaitPage).
        if (available) {
            encryptionStatus = EncryptionStatus.Busy
        }
    }

    function encrypt() {
        call("BeginEncryption", undefined,
             function() {
                 encryptionStatus = EncryptionStatus.Busy
             },
             function(error, message) {
                 errorString = error
                 errorMessage = message
                 encryptionStatus = EncryptionStatus.Error
             }
        )
    }

    function finalize() {
        call("FinalizeEncryption")
    }

    function prepare(passphrase, overwriteType) {
        call("PrepareToEncrypt", [passphrase, overwriteType])
    }

    function encryptionFinished(success, error) {
        encryptionStatus = success ? EncryptionStatus.Encrypted : EncryptionStatus.Error
    }
}
