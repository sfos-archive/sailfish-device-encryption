/*
 * Copyright (c) 2019 Jolla Ltd.
 *
 * License: Proprietary
 */

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.DBus 2.0
import Sailfish.Encryption 1.0
import org.nemomobile.devicelock 1.0
import org.nemomobile.systemsettings 1.0

Page {
    id: page

    // Device lock autentication

    // threshold above which we may reset without charger
    readonly property int batteryThreshold: 15
    readonly property bool batteryChargeOk: battery.chargePercentage > batteryThreshold
    // To be checked
    readonly property bool applicationActive: Qt.application.active

    property EncryptionService encryptionService

    function createBackupLink() {
        //: A link to Settings | System | Backup
        //: Action or verb that can be used for %1 in settings_encryption-la-encrypt_user_data_warning and
        //: settings_encryption-la-encrypt_user_data_description
        //: Strongly proposing user to do a backup.
        //% "Back up"
        var backup = qsTrId("settings_encryption-la-back_up")
        return "<a href='backup'>" + backup + "</a>"
    }

    BatteryStatus {
        id: battery
    }

    USBSettings {
        id: usbSettings
    }

    EncryptionSettings {
        id: encryptionSettings

        onEncryptingHome: lipstick.startEncryptionPreparation()
        onEncryptHomeError: console.warn("Home encryption failed. Maybe token expired.")
    }

    Component {
        id: encryptionServiceComponent
        EncryptionService {}
    }

    DBusInterface {
        id: dsmeDbus
        bus: DBus.SystemBus
        service: "com.nokia.dsme"
        path: "/com/nokia/dsme/request"
        iface: "com.nokia.dsme.request"
    }

    DBusInterface {
        id: lipstick
        bus: DBus.SystemBus
        service: "org.nemomobile.lipstick"
        path: "/shutdown"
        iface: "org.nemomobile.lipstick"

        function startEncryptionPreparation() {
            lipstick.call("setShutdownMode", ["reboot"],
                          function(success) {
                              prepareEncryption.running = true
                          },

                          function(error, message) {
                              console.info("Error occured when entering to reboot mode:", error, "message:", message)
                          }
                          )
        }
    }

    Timer {
        id: prepareEncryption

        property string securityCode

        interval: 3000
        onTriggered: {
            page.encryptionService = encryptionServiceComponent.createObject(root)
            page.encryptionService.prepare(securityCode, "zero")
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        VerticalScrollDecorator {}

        Column {
            id: content
            width: parent.width

            PageHeader {
                //% "Encryption"
                title: qsTrId("settings_encryption-he-encryption")
            }

            Item {
                id: batteryWarning

                width: parent.width - 2*Theme.horizontalPageMargin
                height: Math.max(batteryIcon.height, batteryText.height)
                x: Theme.horizontalPageMargin
                visible: !page.batteryChargeOk && !encryptionSettings.homeEncrypted

                Image {
                    id: batteryIcon
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-l-battery"
                }

                Label {
                    id: batteryText

                    anchors {
                        left: batteryIcon.right
                        leftMargin: Theme.paddingMedium
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    font.pixelSize: Theme.fontSizeMedium
                    color: Theme.highlightColor
                    wrapMode: Text.Wrap
                    text: battery.chargerStatus === BatteryStatus.Connected
                          ? //: Battery low warning for device reset when charger is attached. Same as settings_reset-la-battery_charging
                            //% "Battery level low. Do not remove the charger."
                            qsTrId("settings_encryption-la-battery_charging")
                          : //: Battery low warning for device reset when charger is not attached. Same as settings_reset-la-battery_level_low
                            //% "Battery level too low."
                            qsTrId("settings_encryption-la-battery_level_low")
                }
            }

            Item {
                width: 1
                height: Theme.paddingLarge
                visible: batteryWarning.visible
            }

            Item {
                id: mtpWarning

                width: parent.width - 2*Theme.horizontalPageMargin
                height: Math.max(mtpIcon.height, mtpText.height)
                x: Theme.horizontalPageMargin
                visible: usbSettings.currentMode == usbSettings.MODE_MTP && !encryptionSettings.homeEncrypted

                Image {
                    id: mtpIcon
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-usb"
                }

                Label {
                    id: mtpText

                    anchors {
                        left: mtpIcon.right
                        leftMargin: Theme.paddingMedium
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    font.pixelSize: Theme.fontSizeMedium
                    color: Theme.highlightColor
                    wrapMode: Text.Wrap
                    text: //: USB MTP mode disconnect warning
                          //% "Media transfer (MTP) will be disconnected."
                          qsTrId("settings_encryption-la-mtp_disconnect")
                }
            }

            Item {
                width: 1
                height: Theme.paddingLarge
                visible: mtpWarning.visible
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2*Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                linkColor: Theme.primaryColor
                textFormat: Text.AutoText
                visible: !encryptionSettings.homeEncrypted

                //: Takes "Back up" (settings_encryption-la-back_up) formatted hyperlink as parameter.
                //: This is done because we're creating programmatically a hyperlink for it.
                //% "This will erase all user data from the device. "
                //% "This means losing user data that you have added to the device, reverts apps to clean state, accounts, contacts, photos and other media.<br><br>"
                //% "%1 user data to memory card before encrypting the device."
                text: qsTrId("settings_encryption-la-encrypt_user_data_description").arg(createBackupLink())

                onLinkActivated: pageStack.animatorPush("Sailfish.Vault.MainPage")
            }

            Item {
                width: parent.width
                height: Theme.paddingLarge
                visible: !encryptionSettings.homeEncrypted
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthMedium
                visible: !encryptionSettings.homeEncrypted

                //% "Encrypt"
                text: qsTrId("settings_encryption-bt-encrypt")
                onClicked: {
                    var obj = pageStack.animatorPush(Qt.resolvedUrl("HomeEncryptionDisclaimer.qml"), {
                                                         "encryptionSettings": encryptionSettings
                                                     })

                    var mandatoryDeviceLock

                    obj.pageCompleted.connect(function(p) {
                        p.accepted.connect(function() {
                            mandatoryDeviceLock = p.acceptDestinationInstance
                            p.acceptDestinationInstance.authenticated.connect(function(authenticationToken) {
                                // Enters to "reboot" mode but does not execute actual reboot.
                                prepareEncryption.securityCode = mandatoryDeviceLock.securityCode
                                encryptionSettings.encryptHome(authenticationToken)
                            })
                            p.acceptDestinationInstance.canceled.connect(function() {
                                pageStack.pop(page)
                            })
                        })
                        p.canceled.connect(function() {
                            pageStack.pop(page)
                        })
                    })
                }
                enabled: (page.batteryChargeOk || battery.chargerStatus === BatteryStatus.Connected) && !encryptionSettings.homeEncrypted
            }



            ViewPlaceholder {
                enabled: encryptionSettings.homeEncrypted

                //: Placeholder which is shown when home is already encrypted
                //% "User data is already encrypted"
                text: qsTrId("settings_encryption-la-home_is_already_encrypted")
            }
        }
    }
}
