CFLAGS := -g
prefix := /usr
etcprefix :=
# Define appropiately for your distribution
# DOCDIR := /usr/share/doc/packages/mcelog

# Warning flags added implicitely to CFLAGS in the default rule
# this is done so that even when CFLAGS are overriden we still get
# the additional warnings
# Some warnings require the global optimizer and are only output with 
# -O2/-Os, so that should be tested occasionally
WARNINGS := -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter \
	    -Wstrict-prototypes -Wformat-security -Wmissing-declarations \
	    -Wdeclaration-after-statement

# The on disk database has still many problems (partly in this code and partly
# due to missing support from BIOS), so it's disabled by default. You can 
# enable it here by uncommenting the following line
# CONFIG_DISKDB = 1

all: mcelog

.PHONY: install clean depend

OBJ := p4.o k8.o mcelog.o dmi.o tsc.o core2.o bitfield.o intel.o \
       nehalem.o dunnington.o tulsa.o config.o memutil.o
DISKDB_OBJ := diskdb.o dimm.o db.o
CLEAN := mcelog dmi tsc dbquery .depend .depend.X dbquery.o ${DISKDB_OBJ}
DOC := mce.pdf smbios.spec

ADD_DEFINES :=

ifdef CONFIG_DISKDB
ADD_DEFINES := -DCONFIG_DISKDB=1
OBJ += ${DISKDB_OBJ}

all: dbquery
endif

SRC := $(OBJ:.o=.c)

mcelog: ${OBJ}

# dbquery intentionally not installed by default
install: mcelog
	install -m 755 -p mcelog ${prefix}/sbin/mcelog
	install -m 644 -p mcelog.8 ${prefix}/share/man/man8
	if [ -f /etc/mcelog.conf ] ; then \
		install -m 644 -p mcelog.conf ${etcprefix}/etc/mcelog.conf-NEW ;\
	else 									\
		install -m 644 -p mcelog.conf ${etcprefix}/etc/mcelog.conf ;	\
	fi
ifdef DOCDIR
	install -m 644 -p ${DOC} ${DOCDIR} 
else
	echo
	echo "Consider defining DOCDIR to install additional documentation"
endif
	echo
	echo "call mcelog regularly from your crontab"

clean:
	rm -f ${CLEAN} ${OBJ} 

tsc:    tsc.c
	gcc -o tsc ${CFLAGS} -DSTANDALONE tsc.c ${LDFLAGS}

dbquery: db.o dbquery.o memutil.o

depend: .depend

%.o: %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(WARNINGS) $(ADD_DEFINES) -o $@ $<

.depend: ${SRC}
	${CC} -MM -I. ${SRC} > .depend.X && mv .depend.X .depend

include .depend

Makefile: .depend

.PHONY: iccverify

# run the icc static verifier over sources. you need the intel compiler installed for this
DISABLED_DIAGS := -diag-disable 188,271,869,2259,981,12072,181,12331,1572

iccverify:
	icc -Wall -diag-enable sv3 $(DISABLED_DIAGS) $(ADD_DEFINES) $(SRC)	
