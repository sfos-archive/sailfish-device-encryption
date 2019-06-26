TEMPLATE = lib
CONFIG += qt create_pc \
    static \
    create_prl \
    no_install_prl \
    link_pkgconfig

QT = core

TARGET = sailfishdeviceencryption
TARGET = $$qtLibraryTarget($$TARGET)
TARGETPATH = $$[QT_INSTALL_LIBS]

SOURCES += \
    sailfishdeviceencryption.cpp

HEADERS += \
    sailfishdeviceencryption.h

PKGCONFIG += \
    libcryptsetup

develheaders.path = /usr/include/lib$$TARGET
develheaders.files = $$HEADERS

target.path = $$TARGETPATH

pkgconfig.files = $$TARGET.pc
pkgconfig.path = $$target.path/pkgconfig

QMAKE_PKGCONFIG_NAME = lib$$TARGET
QMAKE_PKGCONFIG_DESCRIPTION = Sailfish Device Encryption
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_INCDIR = $$develheaders.path
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
QMAKE_PKGCONFIG_REQUIRES = libcryptsetup
QMAKE_PKGCONFIG_VERSION = $$VERSION

INSTALLS += target develheaders pkgconfig
