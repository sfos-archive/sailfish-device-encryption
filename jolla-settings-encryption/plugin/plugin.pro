TEMPLATE = lib
TARGET = settingsencryptionplugin
QT += qml quick dbus
CONFIG += plugin link_pkgconfig

MODULENAME = Sailfish/Encryption
TARGETPATH = $$[QT_INSTALL_QML]/$$MODULENAME
TRANSLATIONS_PATH = /usr/share/translations

SOURCES += $$PWD/plugin.cpp \
    $$PWD/homeinfo.cpp

HEADERS += $$PWD/encryptionstatus.h \
    $$PWD/homeinfo.h

PKGCONFIG += \
    systemsettings

import.files = qmldir EncryptionService.qml
import.path = $$TARGETPATH
target.path = $$TARGETPATH

settingsentry.path = /usr/share/jolla-settings/entries
settingsentry.files = encryption.json

settingspages.path = /usr/share/jolla-settings/pages/encryption
settingspages.files = *.qml

INSTALLS += target import settingsentry settingspages

OTHER_FILES += qmldir *.qml *.json

# translations
TS_FILE = $$OUT_PWD/settings-encryption.ts
EE_QM = $$OUT_PWD/settings-encryption_eng_en.qm

ts.commands += lupdate $$IN_PWD/.. -ts $$TS_FILE
ts.CONFIG += no_check_exist no_link
ts.output = $$TS_FILE
ts.input = .

ts_install.files = $$TS_FILE
ts_install.path = /usr/share/translations/source
ts_install.CONFIG += no_check_exist


# should add -markuntranslated "-" when proper translations are in place (or for testing)
engineering_english.commands += lrelease -idbased $$TS_FILE -qm $$EE_QM
engineering_english.CONFIG += no_check_exist no_link
engineering_english.depends = ts
engineering_english.input = $$TS_FILE
engineering_english.output = $$EE_QM

engineering_english_install.path = /usr/share/translations
engineering_english_install.files = $$EE_QM
engineering_english_install.CONFIG += no_check_exist

QMAKE_EXTRA_TARGETS += ts engineering_english

PRE_TARGETDEPS += ts engineering_english

INSTALLS += ts_install engineering_english_install
