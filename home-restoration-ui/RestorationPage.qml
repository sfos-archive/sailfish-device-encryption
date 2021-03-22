/*
 * Copyright (c) 2021 Jolla Ltd.
 *
 * License: Proprietary
 */

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

