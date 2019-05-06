TEMPLATE = app
TARGET = sailfish-unlock-ui

CONFIG -= qt
CONFIG += link_pkgconfig
CONFIG += sailfish-minui-resources

PKGCONFIG += \
    sailfish-minui \
    libudev

HEADERS += \
    pin.h

SOURCES += \
    main.cpp \
    pin.cpp

OTHER_FILES += \
    rpm/sailfish-unlock-ui.spec

INCLUDEPATH += ../inih

labels.ids = \
    label-unlock-id
labels.heading = true
labels.size = Large
labels.alignment = Center

SAILFISH_MINUI_TRANSLATIONS = \
    labels

target.path = /usr/sbin

systemd.files = systemd/*
systemd.path = /lib/systemd/system
systemd.extra = mkdir -p ${INSTALL_ROOT}/lib/systemd/system/sysinit.target.wants;ln -fs ../sailfish-unlock-agent.path ${INSTALL_ROOT}/lib/systemd/system/sysinit.target.wants/

INSTALLS += \
    systemd \
    target

inih.target = ini.o
inih.depends = ../inih/ini.c
inih.commands = $(CC) -c ../inih/ini.c -o ini.o
QMAKE_EXTRA_TARGETS += inih

PRE_TARGETDEPS += ini.o
LIBS += ini.o
