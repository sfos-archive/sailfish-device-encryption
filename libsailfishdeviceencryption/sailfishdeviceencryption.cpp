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
