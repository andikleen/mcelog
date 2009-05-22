CFLAGS := -g -Wall
prefix := /usr
etcprefix :=
# Define appropiately for your distribution
# DOCDIR := /usr/share/doc/packages/mcelog

# The on disk database has still many problems (partly in this code and partly
# due to missing support from BIOS), so it's disabled by default. You can 
# enable it here by uncommenting the following line
# CONFIG_DISKDB = 1

all: mcelog

.PHONY: install clean depend

OBJ := p4.o k8.o mcelog.o dmi.o tsc.o core2.o bitfield.o intel.o \
       nehalem.o dunnington.o tulsa.o config.o memutil.o
DISKDB_OBJ := diskdb.o dimm.o db.o
SRC := $(OBJ:.o=.c)
CLEAN := mcelog dmi tsc dbquery .depend .depend.X dbquery.o ${DISKDB_OBJ}
DOC := mce.pdf smbios.spec

ifdef CONFIG_DISKDB
CFLAGS += -DCONFIG_DISKDB=1
OBJ += ${DISKDB_OBJ}

all: dbquery
endif

mcelog: ${OBJ}

# dbquery intentionally not installed by default
install: mcelog
	cp mcelog ${prefix}/sbin/mcelog
	cp mcelog.8 ${prefix}/share/man/man8
	cp mcelog.conf ${etcprefix}/etc
ifdef DOCDIR
	cp ${DOC} ${DOCDIR} 
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

.depend: ${SRC}
	${CC} -MM -I. ${SRC} > .depend.X && mv .depend.X .depend

include .depend

Makefile: .depend
