TEMPLATE = app
TARGET = sailfish-unlock-ui

CONFIG -= qt
CONFIG += link_pkgconfig
CONFIG += sailfish-minui-resources

PKGCONFIG += \
    sailfish-minui \
    libudev \
    glib-2.0

HEADERS += \
    pin.h

SOURCES += \
    main.cpp \
    pin.cpp

OTHER_FILES += \
    rpm/sailfish-unlock-ui.spec

labels.ids = \
    sailfish-device-encryption-unlock-ui-la-enter_security_code
labels.heading = true
labels.size = ExtraLarge
labels.alignment = Center

SAILFISH_MINUI_TRANSLATIONS = \
    labels

target.path = /usr/libexec

systemd.files = systemd/*
systemd.path = /lib/systemd/system
systemd.extra = mkdir -p ${INSTALL_ROOT}/lib/systemd/system/sysinit.target.wants;ln -fs ../sailfish-unlock-agent.path ${INSTALL_ROOT}/lib/systemd/system/sysinit.target.wants/

INSTALLS += \
    systemd \
    target
