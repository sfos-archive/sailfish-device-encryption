/**
 * Copyright (c) 2019 Open Mobile Platform LLC.
 *
 * License: Proprietary
 */

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
