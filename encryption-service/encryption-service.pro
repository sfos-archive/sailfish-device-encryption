TEMPLATE = aux

HEADERS += \
    dbus.h \
    encrypt.h \
    manage.h

SOURCES += \
    dbus.c \
    encrypt.c \
    main.c \
    manage.c

OTHER_FILES += \
    dbus-org.sailfishos.EncryptionService.service \
    home-mount-settle.service \
    org.sailfishos.EncryptionService.*
