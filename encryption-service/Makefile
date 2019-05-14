CFLAGS += -O2 -Wall
override CFLAGS += $(shell pkg-config --cflags glib-2.0)
LIBS += $(shell pkg-config --libs glib-2.0)
override CFLAGS += $(shell pkg-config --cflags udisks2)
LIBS += $(shell pkg-config --libs udisks2)
override CFLAGS += $(shell pkg-config --cflags libdbusaccess)
LIBS += $(shell pkg-config --libs libdbusaccess)

BINDIR = /usr/libexec
DATADIR = /usr/share/sailfish-device-encryption
UNITDIR = /lib/systemd/system
DBUSNAME = org.sailfishos.EncryptionService
DBUS_SYSTEM_DIR = /usr/share/dbus-1/system.d
DBUS_SERVICE_DIR = /usr/share/dbus-1/system-services
INSTALL = install -D

all: encryption-service

encryption-service: dbus.o encrypt.o manage.o main.c
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

install-preparation: preparation/home-encryption-preparation.service \
		preparation/home-encryption-preparation.sh \
		preparation/home-encryption-finish.sh
	$(INSTALL) -m0644 preparation/home-encryption-preparation.service \
		$(DESTDIR)/$(UNITDIR)/home-encryption-preparation.service
	$(INSTALL) -m0700 preparation/home-encryption-preparation.sh \
		$(DESTDIR)/$(DATADIR)/home-encryption-preparation.sh
	$(INSTALL) -m0700 preparation/home-encryption-finish.sh \
		$(DESTDIR)/$(DATADIR)/home-encryption-finish.sh

install-service-file: dbus-$(DBUSNAME).service
	$(INSTALL) -m0644 $< $(DESTDIR)/$(UNITDIR)/$<

install-dbus-file: $(DBUSNAME).service
	$(INSTALL) -m0644 $< $(DESTDIR)/$(DBUS_SERVICE_DIR)/$<

install-bus-config: $(DBUSNAME).conf
	$(INSTALL) -m0644 $< $(DESTDIR)/$(DBUS_SYSTEM_DIR)/$<

install: encryption-service \
		install-bus-config \
		install-dbus-file \
		install-service-file \
		install-preparation
	$(INSTALL) $< $(DESTDIR)/$(BINDIR)/sailfish-encryption-service

clean:
	rm -f dbus.o encrypt.o manage.o encryption-service
