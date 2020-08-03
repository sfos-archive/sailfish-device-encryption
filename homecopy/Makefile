CFLAGS += -O2 -Wall
override CFLAGS += $(shell pkg-config --cflags glib-2.0)
LIBS += $(shell pkg-config --libs glib-2.0)
override CFLAGS += $(shell pkg-config --cflags udisks2)
LIBS += $(shell pkg-config --libs udisks2)
override CFLAGS += $(shell pkg-config --cflags libdbusaccess)
LIBS += $(shell pkg-config --libs libdbusaccess)
override CFLAGS += $(shell pkg-config --cflags sailfishaccesscontrol)
LIBS += $(shell pkg-config --libs sailfishaccesscontrol)

BINDIR = /usr/libexec
DATADIR = /usr/share/sailfish-device-encryption
UNITDIR = /usr/lib/systemd/system
USERUNITDIR = /usr/lib/systemd/user
DBUSNAME = org.sailfishos.HomeCopyService
DBUS_SYSTEM_DIR = /usr/share/dbus-1/system.d
DBUS_SERVICE_DIR = /usr/share/dbus-1/system-services
INSTALL = install -D

all: home-copy-service

home-copy-service: homecopy.o copyservice.c
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

install-script: home-encryption-copy.service \
		home-encryption-copy.sh \
		home-restore.service \
		home-restore.sh \
		home-restore-ui.service
	$(INSTALL) -m0644 home-encryption-copy.service \
		$(DESTDIR)/$(UNITDIR)/home-encryption-copy.service
	$(INSTALL) -m0700 home-encryption-copy.sh \
		$(DESTDIR)/$(DATADIR)/home-encryption-copy.sh
	$(INSTALL) -m0644 home-restore.service \
		$(DESTDIR)/$(UNITDIR)/home-restore.service
	$(INSTALL) -m0700 home-restore.sh \
		$(DESTDIR)/$(DATADIR)/home-restore.sh
	$(INSTALL) -m0644 home-restore-ui.service \
		$(DESTDIR)/$(USERUNITDIR)/home-restore-ui.service

install-service-file: dbus-$(DBUSNAME).service
	$(INSTALL) -m0644 $< $(DESTDIR)/$(UNITDIR)/$<

install-dbus-file: $(DBUSNAME).service
	$(INSTALL) -m0644 $< $(DESTDIR)/$(DBUS_SERVICE_DIR)/$<

install-bus-config: $(DBUSNAME).conf
	$(INSTALL) -m0644 $< $(DESTDIR)/$(DBUS_SYSTEM_DIR)/$<

install: home-copy-service \
		install-bus-config \
		install-dbus-file \
		install-service-file \
		install-script
	$(INSTALL) $< $(DESTDIR)/$(BINDIR)/sailfish-home-copy-service

clean:
	rm -f homecopy.o home-copy-service
