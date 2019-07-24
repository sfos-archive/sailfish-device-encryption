import QtQuick 2.0
import Nemo.DBus 2.0
import Nemo.FileManager 1.0
import org.nemomobile.configuration 1.0
import Sailfish.Encryption 1.0

DBusInterface {
    id: encryptionService

    // This introspects the interface. Thus, starting the dbus service.
    bus: DBus.SystemBus
    service: "org.sailfishos.EncryptionService"
    path: "/org/sailfishos/EncryptionService"
    iface: "org.sailfishos.EncryptionService"
    signalsEnabled: true

    property string errorString
    property string errorMessage
    property int encryptionStatus
    property bool available
    readonly property bool busy: encryptionStatus == EncryptionStatus.Busy

    // DBusInterface is a QObject so no child items
    property FileWatcher encryptHome: FileWatcher {
        id: encryptHome
        fileName: "/var/lib/sailfish-device-encryption/encrypt-home"
    }

    readonly property DBusInterface nameOwner: DBusInterface {
        bus: DBus.SystemBus
        service: 'org.freedesktop.DBus'
        path: '/org/freedesktop/DBus'
        iface: 'org.freedesktop.DBus'
        Component.onCompleted: call("NameHasOwner", "org.sailfishos.EncryptionService", function (hasOwner) {
            encryptionService.available = hasOwner && encryptHome.exists

            // Move to busy state right after name has owner changed. So that
            // user do not see text change from Idle to Busy (encryption is started
            // when we hit the PleaseWaitPage).
            if (encryptionService.available) {
                encryptionService.encryptionStatus = EncryptionStatus.Busy
            }
        })
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
