TEMPLATE = app
TARGET = sailfish-home-restoration
TARGETPATH = /usr/libexec

QT += qml quick

SOURCES += main.cpp

OTHER_FILES += qmldir *.qml $${TARGET}.privileges

target.path = $$TARGETPATH

qml.path = /usr/share/$$TARGET
qml.files = *.qml

privileges.path = /usr/share/mapplauncherd/privileges.d/
privileges.files = *.privileges

INSTALLS += target qml privileges

CONFIG += link_pkgconfig

