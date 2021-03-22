TEMPLATE = subdirs
SUBDIRS += \
    encryption-service \
    home-restoration-ui \
    homecopy \
    libsailfishdeviceencryption \
    jolla-settings-encryption \
    sailfish-unlock-ui

OTHER_FILES += \
        rpm/sailfish-device-encryption.spec

jolla-settings-encryption.depends = libsailfishdeviceencryption
