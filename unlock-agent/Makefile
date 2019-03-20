CFLAGS=-O2
LDFLAGS=-flto
INSTALL=install
INSTALL_PROGRAM=$(INSTALL)
BINDIR=/usr/bin
SRCDIR=

all: agent

libinih.a: ../inih/ini.c
	$(CC) $(CFLAGS) -I$(SRCDIR)../inih -c -o $@ $<

agent: agent.c libinih.a
	$(CC) $(CFLAGS) $(LDFLAGS) -I$(SRCDIR)../inih -o $@ agent.c libinih.a

install-agent: agent
	$(INSTALL_PROGRAM) -D $(SRCDIR)$< $(DESTDIR)$(BINDIR)/unlock-$<

install: install-agent

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/unlock-agent

clean:
	rm -f libinih.a
	rm -f agent
