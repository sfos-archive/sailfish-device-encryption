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

import QtQuick 2.2
import Sailfish.Silica 1.0
import org.nemomobile.devicelock 1.0
import org.nemomobile.systemsettings 1.0

Dialog {
    id: dialog

    readonly property int batteryThreshold: 50
    readonly property bool batteryChargeOk: battery.chargePercentage > batteryThreshold

    property EncryptionSettings encryptionSettings

    function createBackupLink() {
        //: A link to Settings | System | Backup
        //: Action or verb that can be used for %1 in settings_encryption-la-encrypt_user_data_warning and
        //: settings_encryption-la-encrypt_user_data_description
        //: Strongly proposing user to do a backup.
        //% "Back up"
        var backup = qsTrId("settings_encryption-la-back_up")
        return "<a href='backup'>" + backup + "</a>"
    }

    acceptDestination: "com.jolla.settings.system.MandatoryDeviceLockInputPage"
    acceptDestinationAction: PageStackAction.Replace
    acceptDestinationProperties: {
        "authorization": encryptionSettings.authorization
    }

    canAccept: (batteryChargeOk || battery.chargerStatus === BatteryStatus.Connected)
                && encryptionSettings
                && encryptionSettings.authorization.status === Authorization.ChallengeIssued

    Component.onCompleted: encryptionSettings.authorization.requestChallenge()

    onAccepted: acceptDestinationInstance.authenticate()

    BatteryStatus {
        id: battery
    }

    SecurityCodeSettings {
        id: securityCode
    }

    SilicaFlickable {
        contentHeight: content.height
        anchors.fill: parent

        Column {
            id: content

            width: parent.width

            DialogHeader {
                dialog: dialog
            }

            Item {
                id: batteryWarning

                width: parent.width - 2*Theme.horizontalPageMargin
                height: Math.max(batteryIcon.height, batteryText.height) + Theme.paddingLarge
                x: Theme.horizontalPageMargin
                visible: !dialog.batteryChargeOk

                Image {
                    id: batteryIcon
                    anchors {
                        verticalCenter: parent.verticalCenter
                        verticalCenterOffset: -Theme.paddingLarge / 2
                    }
                    source: "image://theme/icon-l-battery"
                }

                Label {
                    id: batteryText

                    anchors {
                        left: batteryIcon.right
                        leftMargin: Theme.paddingMedium
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        verticalCenterOffset: -Theme.paddingLarge / 2
                    }
                    font.pixelSize: Theme.fontSizeMedium
                    color: Theme.highlightColor
                    wrapMode: Text.Wrap
                    text: battery.chargerStatus === BatteryStatus.Connected
                          ? //: Battery low warning for device encryption when charger is attached.
                            //% "Battery level low. Do not remove the charger."
                            qsTrId("settings_encryption-la-battery_charging")
                          : //: Battery low warning for device encryption when charger is not attached.
                            //% "Battery level too low."
                            qsTrId("settings_encryption-la-battery_level_low")
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2*Theme.horizontalPageMargin
                height: implicitHeight + Theme.paddingLarge
                font {
                    family: Theme.fontFamilyHeading
                    pixelSize: Theme.fontSizeExtraLarge
                }
                color: Theme.highlightColor

                //% "Did you remember to backup?"
                text: qsTrId("settings_encryption-he-backup_reminder")
                wrapMode: Text.Wrap
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2*Theme.horizontalPageMargin
                height: implicitHeight + Theme.paddingLarge
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
                linkColor: Theme.primaryColor
                textFormat: Text.AutoText

                //: Takes "Back up" (settings_encryption-la-back_up) formatted hyperlink as parameter.
                //: This is done because we're creating programmatically a hyperlink for it.
                //% "Accepting this will erase all user data on the device. "
                //% "This means losing user data that you have added to the device (e.g. reverts apps to clean state, accounts, contacts, photos and other media). "
                //% "%1 user data to memory card.<br><br> "
                //% "This is an irreversible change and you will need to enter a security code on all future boots in order to access your data.<br><br>"
                //% "If you accept the device will reboot automatically and you will be prompted for the security code before encrypting user data."
                text: qsTrId("settings_encryption-la-encrypt_user_data_warning").arg(createBackupLink())
                wrapMode: Text.Wrap

                onLinkActivated: pageStack.animatorPush("Sailfish.Vault.MainPage")
            }

            Label {
                //% "How do I make a backup and restore it?"
                readonly property string title: qsTrId("settings_encryption-la-make_backup_and_restore_it")
                readonly property string url: "https://sailfishos.org/article/backup-and-restore"

                x: Theme.horizontalPageMargin
                width: parent.width - 2*Theme.horizontalPageMargin
                height: implicitHeight + Theme.paddingLarge
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
                linkColor: Theme.primaryColor
                textFormat: Text.AutoText
                text: "<a href='" + url + "'>" + title +"</a>"
                wrapMode: Text.Wrap
                onLinkActivated: Qt.openUrlExternally(link)
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2*Theme.horizontalPageMargin
                height: implicitHeight + Theme.paddingLarge
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
                text: securityCode.set
                        //% "You will need to enter your security code before user data can be encrypted."
                        ? qsTrId("settings_encryption-la-encrypt_user_data_security_code_notice")
                        //% "You will need to set a security code before user data can be encrypted."
                        : qsTrId("settings_encryption-la-encrypt_user_data_create_security_code_notice")
                wrapMode: Text.Wrap
            }
        }
    }
}

