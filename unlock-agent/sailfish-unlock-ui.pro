TEMPLATE = app
TARGET = sailfish-unlock-ui

QMAKE_CFLAGS += -std=c99
# our gcc generates false positive warnings
QMAKE_CFLAGS += -Wno-missing-field-initializers
# as qmake does not grok CPPFLAGS, use CFLAGS
QMAKE_CFLAGS += -D_GNU_SOURCE

CONFIG -= qt
CONFIG += link_pkgconfig
CONFIG += sailfish-minui-resources

PKGCONFIG += \
    sailfish-minui \
    sailfish-minui-dbus \
    libudev \
    glib-2.0

HEADERS += \
    compositor.h \
    devicelocksettings.h \
    pin.h

SOURCES += \
    main.cpp \
    compositor.cpp \
    devicelocksettings.cpp \
    touchinput.c \
    pin.cpp

OTHER_FILES += \
    rpm/sailfish-unlock-ui.spec

labels.ids = \
    sailfish-device-encryption-unlock-ui-la-enter_security_code
labels.heading = true
labels.size = ExtraLarge
labels.alignment = Center

warningLabels.ids = \
    sailfish-device-encryption-unlock-ui-la-incorrect_security_code \
    sailfish-device-encryption-unlock-ui-la-last_chance
warningLabels.size = Small
warningLabels.alignment = Center

SAILFISH_MINUI_TRANSLATIONS = \
    labels \
    warningLabels

target.path = /usr/libexec

systemd.files = systemd/*
systemd.path = /usr/lib/systemd/system
systemd.extra = mkdir -p ${INSTALL_ROOT}/usr/lib/systemd/system/sysinit.target.wants;ln -fs ../sailfish-unlock-agent.path ${INSTALL_ROOT}/usr/lib/systemd/system/sysinit.target.wants/

INSTALLS += \
    systemd \
    target
