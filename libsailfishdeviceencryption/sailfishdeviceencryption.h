#ifndef SAILFISH_DEVICE_ENCRYPTION
#define SAILFISH_DEVICE_ENCRYPTION

#include <libcryptsetup.h>

namespace Sailfish {
namespace DeviceEncryption {

struct crypt_device *getCryptDeviceForHome();
bool isHomeEncrypted();

}
}

#endif
