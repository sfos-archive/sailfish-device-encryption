TEMPLATE = aux

HEADERS += \
    copyservice.h \
    homecopy.h

SOURCES += \
    copyservice.c \
    homecopy.c

OTHER_FILES += \
    dbus-org.sailfishos.HomeCopyService.service \
    org.sailfishos.HomeCopyService.*
