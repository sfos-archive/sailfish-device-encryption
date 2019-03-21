CFLAGS=-O2
LDFLAGS=-flto
INSTALL=install
INSTALL_PROGRAM=$(INSTALL)
BINDIR=/usr/bin
UNITDIR=/lib/systemd/system
SRCDIR=

all: agent

libinih.a: ../inih/ini.c
	$(CC) $(CFLAGS) -I$(SRCDIR)../inih -c -o $@ $<

agent: agent.c libinih.a
	$(CC) $(CFLAGS) $(LDFLAGS) -I$(SRCDIR)../inih -o $@ agent.c libinih.a

install-agent: agent
	$(INSTALL_PROGRAM) -D $(SRCDIR)$< $(DESTDIR)$(BINDIR)/unlock-$<

service-directory:
	install -d $(DESTDIR)$(UNITDIR)
	install -d $(DESTDIR)$(UNITDIR)/sysinit.target.wants

install-path-file: systemd/sailfish-unlock-agent.path service-directory
	install -m0644 $(SRCDIR)$< $(DESTDIR)$(UNITDIR)/sailfish-unlock-agent.path
	ln -s ../sailfish-unlock-agent.path $(DESTDIR)$(UNITDIR)/sysinit.target.wants/

install-service-file: systemd/sailfish-unlock-agent.service service-directory
	install -m0644 $(SRCDIR)$< $(DESTDIR)$(UNITDIR)/sailfish-unlock-agent.service

install: install-agent install-path-file install-service-file

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/unlock-agent

clean:
	rm -f libinih.a
	rm -f agent
