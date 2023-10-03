/****************************************************************************************
** Copyright (c) 2021 - 2023 Jolla Ltd.
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

// Translations for the restoration ui are in jolla-settings-encryption/LayoutTranslations.qml

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.DBus 2.0
import Nemo.Notifications 1.0

Page {
    id: page
    backNavigation: false

    property RestorationService restorationService

    Component {
        id: restorationServiceComponent
        RestorationService {}
    }

    DBusInterface {
        id: lipstick
        bus: DBus.SystemBus
        service: "org.nemomobile.lipstick"
        path: "/shutdown"
        iface: "org.nemomobile.lipstick"

        function setShutdown() {
            lipstick.call("setShutdownMode", ["reboot"])
        }
    }

    Notification {
        id: notification
        // "Restoring user data failed"
        summary: qsTrId("settings_encryption-la-restore-fail-summary")
        // "Data is kept on memorycard"
        body: qsTrId("settings_encryption-la-restore-fail-body")
    }

    Timer {
        id: quitTimer
        interval: 5000
        running: false
        onTriggered: {
            lipstick.setShutdown()
        }
    }

    Timer {
        id: restorationStarter
        interval: 10000
        running: true
        repeat: false
        onTriggered: {
            page.restorationService = restorationServiceComponent.createObject(page)
            page.restorationService.restoreHome()
            page.restorationService.copied.connect(function(success) {
                if(!success) {
                    notification.publish()
                }
                quitTimer.running = true
            })
        }
    }

    BusyLabel {
        running: true
        // "Restoring user data"
        text: qsTrId("settings_encryption-la-restoring-data")
    }
}

