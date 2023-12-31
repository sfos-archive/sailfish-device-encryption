CFLAGS += -O2 -Wall
override CFLAGS += $(shell pkg-config --cflags glib-2.0)
LIBS += $(shell pkg-config --libs glib-2.0)
override CFLAGS += $(shell pkg-config --cflags udisks2)
LIBS += $(shell pkg-config --libs udisks2)
override CFLAGS += $(shell pkg-config --cflags libdbusaccess)
LIBS += $(shell pkg-config --libs libdbusaccess)
override CFLAGS += $(shell pkg-config --cflags openssl)
LIBS += $(shell pkg-config --libs openssl)
override CFLAGS += $(shell pkg-config --cflags sailfishaccesscontrol)
LIBS += $(shell pkg-config --libs sailfishaccesscontrol)

BINDIR = /usr/libexec
DATADIR = /usr/share/sailfish-device-encryption
UNITDIR = /usr/lib/systemd/system
USERUNITDIR = /usr/lib/systemd/user
DBUSNAME = org.sailfishos.EncryptionService
DBUS_SYSTEM_DIR = /usr/share/dbus-1/system.d
DBUS_SERVICE_DIR = /usr/share/dbus-1/system-services
INSTALL = install -D

all: encryption-service

encryption-service: dbus.o encrypt.o manage.o main.c
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

install-preparation: preparation/home-encryption-preparation.service \
		preparation/home-encryption-preparation.sh \
		preparation/home-encryption-finish.sh \
		preparation/50-after-preparation.conf
	$(INSTALL) -m0644 preparation/home-encryption-preparation.service \
		$(DESTDIR)/$(UNITDIR)/home-encryption-preparation.service
	$(INSTALL) -m0700 preparation/home-encryption-preparation.sh \
		$(DESTDIR)/$(DATADIR)/home-encryption-preparation.sh
	$(INSTALL) -m0700 preparation/home-encryption-finish.sh \
		$(DESTDIR)/$(DATADIR)/home-encryption-finish.sh
	$(INSTALL) -m0644 preparation/50-after-preparation.conf \
		$(DESTDIR)/$(UNITDIR)/mount-sd@.service.d/50-after-preparation.conf

install-systemd-confs: systemd/01-home-mount.conf systemd/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-home-mount.conf \
		$(DESTDIR)/$(UNITDIR)/packagekit.service.d/01-home-mount.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(UNITDIR)/aliendalvik.service.d/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(UNITDIR)/connman.service.d/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(UNITDIR)/connman-vpn.service.d/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(UNITDIR)/dbus-org.nemomobile.MmsEngine.service.d/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(UNITDIR)/dbus-org.nemomobile.provisioning.service.d/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(UNITDIR)/mdm_proxy.service.d/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(UNITDIR)/packagekit.service.d/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(UNITDIR)/vnc.service.d/01-prevent-start.conf
	$(INSTALL) -m0644 systemd/01-prevent-start.conf \
		$(DESTDIR)/$(USERUNITDIR)/ambienced.service.d/01-prevent-start.conf

install-home-mount-settle-service: home-mount-settle.service
	$(INSTALL) -m0644 $< $(DESTDIR)/$(UNITDIR)/$<

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
		install-home-mount-settle-service \
		install-preparation \
		install-systemd-confs
	$(INSTALL) $< $(DESTDIR)/$(BINDIR)/sailfish-encryption-service

clean:
	rm -f dbus.o encrypt.o manage.o encryption-service
