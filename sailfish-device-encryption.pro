TEMPLATE = subdirs
SUBDIRS += \
    encryption-service \
    libsailfishdeviceencryption \
    jolla-settings-encryption \
    sailfish-unlock-ui

OTHER_FILES += \
        rpm/sailfish-device-encryption.spec

jolla-settings-encryption.depends = libsailfishdeviceencryption
