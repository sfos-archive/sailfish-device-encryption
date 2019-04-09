CFLAGS += -O2 -Wall
override CFLAGS += $(shell pkg-config --cflags glib-2.0)
LIBS += $(shell pkg-config --libs glib-2.0)
override CFLAGS += $(shell pkg-config --cflags udisks2)
LIBS += $(shell pkg-config --libs udisks2)

BINDIR = /usr/sbin
UNITDIR = /lib/systemd/system
DBUSNAME = org.sailfishos.EncryptionService
DBUS_SYSTEM_DIR = /usr/share/dbus-1/system.d
DBUS_SERVICE_DIR = /usr/share/dbus-1/system-services
INSTALL = install -D

all: encryption-service

encryption-service: dbus.o encrypt.o main.c
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

install-service-file: dbus-$(DBUSNAME).service
	$(INSTALL) $< $(DESTDIR)/$(UNITDIR)/$<

install-dbus-file: $(DBUSNAME).service
	$(INSTALL) $< $(DESTDIR)/$(DBUS_SERVICE_DIR)/$<

install-bus-config: $(DBUSNAME).conf
	$(INSTALL) $< $(DESTDIR)/$(DBUS_SYSTEM_DIR)/$<

install: encryption-service \
		install-bus-config \
		install-dbus-file \
		install-service-file
	$(INSTALL) $< $(DESTDIR)/$(BINDIR)/sailfish-encryption-service

clean:
	rm -f dbus.o encrypt.o encryption-service
