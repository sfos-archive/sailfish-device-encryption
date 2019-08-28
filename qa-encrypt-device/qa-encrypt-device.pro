TEMPLATE = app
TARGET = qa-encrypt-device
TARGETPATH = /usr/lib/startup

QT += core qml

SOURCES += main.cpp

OTHER_FILES += *.qml

target.path = $$TARGETPATH

DEPLOYMENT_PATH = /usr/share/$$TARGET
DEFINES *= DEPLOYMENT_PATH=\"\\\"\"$${DEPLOYMENT_PATH}/\"\\\"\"
qml.path = $$DEPLOYMENT_PATH
qml.files = *.qml

oneshot.path = /usr/lib/oneshot.d
oneshot.files = 50-enable-home-encryption

INSTALLS += target qml oneshot
