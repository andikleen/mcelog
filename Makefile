CFLAGS := -g -Os
prefix := /usr
etcprefix :=
MANDIR := ${prefix}/share/man
# Define appropiately for your distribution
# DOCDIR := /usr/share/doc/packages/mcelog

# Note when changing prefix: some of the non-critical files like
# the manpage or the init script have hardcoded prefixes

# Warning flags added implicitely to CFLAGS in the default rule
# this is done so that even when CFLAGS are overriden we still get
# the additional warnings
# Some warnings require the global optimizer and are only output with
# -O2/-Os, so that should be tested occasionally
WARNINGS := -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter \
	    -Wstrict-prototypes -Wformat-security -Wmissing-declarations \
	    -Wdeclaration-after-statement

TRIGGERS=cache-error-trigger dimm-error-trigger page-error-trigger \
	 socket-memory-error-trigger \
	 bus-error-trigger \
	 iomca-error-trigger \
	 unknown-error-trigger \
	 page-error-pre-sync-soft-trigger \
	 page-error-post-sync-soft-trigger \
	 page-error-counter-replacement-trigger

all: mcelog

.PHONY: install install-nodoc clean depend FORCE

OBJ := p4.o k8.o mcelog.o dmi.o tsc.o core2.o bitfield.o intel.o \
       nehalem.o dunnington.o tulsa.o config.o memutil.o msg.o   \
       eventloop.o leaky-bucket.o memdb.o server.o trigger.o 	 \
       client.o cache.o sysfs.o yellow.o page.o rbtree.o 	 \
       sandy-bridge.o ivy-bridge.o haswell.o		 	 \
       broadwell_de.o broadwell_epex.o skylake_xeon.o		 \
       denverton.o i10nm.o sapphire.o granite.o			 \
       msr.o bus.o unknown.o lookup_intel_cputype.o
CLEAN := mcelog dmi tsc dbquery .depend .depend.X dbquery.o \
	version.o version.c version.tmp cputype.h cputype.tmp \
	lookup_intel_cputype.c lookup_intel_cputype.tmp
DOC := mce.pdf

ADD_DEFINES :=

SRC := $(OBJ:.o=.c)

mcelog: ${OBJ} version.o

# dbquery intentionally not installed by default
install: install-nodoc mcelog.conf.5 mcelog.triggers.5
	mkdir -p $(DESTDIR)$(MANDIR)/man5 $(DESTDIR)$(MANDIR)/man8
	install -m 644 -p mcelog.8 $(DESTDIR)$(MANDIR)/man8
	install -m 644 -p mcelog.conf.5 $(DESTDIR)$(MANDIR)/man5
	install -m 644 -p mcelog.triggers.5 $(DESTDIR)$(MANDIR)/man5
ifdef DOCDIR
	install -d 755 $(DESTDIR)${DOCDIR}
	install -m 644 -p ${DOC} $(DESTDIR)${DOCDIR}
else
	echo
	echo "Consider defining DOCDIR to install additional documentation"
endif

install-nodoc: mcelog mcelog.conf
	mkdir -p $(DESTDIR)${etcprefix}/etc/mcelog $(DESTDIR)${prefix}/sbin
	install -m 755 -p mcelog $(DESTDIR)${prefix}/sbin/mcelog
	install -m 644 -p -b mcelog.conf $(DESTDIR)${etcprefix}/etc/mcelog/mcelog.conf
	for i in ${TRIGGERS} ; do 						\
		install -m 755 -p -b triggers/$$i $(DESTDIR)${etcprefix}/etc/mcelog ; 	\
	done

mcelog.conf.5: mcelog.conf config-intro.man
	./genconfig.py mcelog.conf config-intro.man > mcelog.conf.5

clean: test-clean
	rm -f ${CLEAN} ${OBJ}

tsc:    tsc.c
	$(CC) -o tsc ${CFLAGS} -DSTANDALONE tsc.c ${LDFLAGS}

dbquery: db.o dbquery.o memutil.o

depend: .depend

%.o: %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(WARNINGS) $(ADD_DEFINES) -o $@ $<

version.tmp: FORCE
	( printf "char version[] = \"" ; 			\
	if test -e .os_version; then				\
		cat .os_version	| tr -d '\n' ;			\
	elif command -v git >/dev/null; then 			\
		if [ -d .git ] ; then 				\
			git describe --tags HEAD | tr -d '\n'; 	\
		else 						\
			printf "unknown" ; 			\
		fi ;						\
	else							\
		printf "unknown" ;				\
	fi ;							\
	printf '";\n'						\
	) > version.tmp

version.c: version.tmp
	cmp version.tmp version.c || mv version.tmp version.c

cputype.tmp lookup_intel_cputype.tmp &: cputype.table
	./mkcputype

cputype.h: cputype.tmp
	cmp cputype.tmp cputype.h || mv cputype.tmp cputype.h

lookup_intel_cputype.c: lookup_intel_cputype.tmp
	cmp lookup_intel_cputype.c lookup_intel_cputype.tmp || mv lookup_intel_cputype.tmp lookup_intel_cputype.c

lookup_intel_cputype.o: cputype.h config.h

.depend: ${SRC}
	[ -f cputype.h ] || touch cputype.h
	${CC} -MM -I. ${SRC} > .depend.X && mv .depend.X .depend

include .depend

Makefile: .depend

.PHONY: iccverify src test

# run the icc static verifier over sources. you need the intel compiler installed for this
DISABLED_DIAGS := -diag-disable 188,271,869,2259,981,12072,181,12331,1572

iccverify:
	icc -Wall -diag-enable sv3 $(DISABLED_DIAGS) $(ADD_DEFINES) $(SRC)

clangverify:
	clang --analyze $(ADD_DEFINES) $(SRC)

src:
	echo $(SRC)

config-test: config.c
	$(CC) -DTEST=1 config.c -o config-test

test:
	$(MAKE) -C tests test DEBUG=""

VALGRIND=valgrind --leak-check=full

valgrind-test:
	$(MAKE) -C tests test DEBUG="${VALGRIND}"

test-clean:
	$(MAKE) -C tests clean
