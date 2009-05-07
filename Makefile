CFLAGS := -g -Wall
prefix := /usr
# Define appropiately for your distribution
# DOCDIR := /usr/share/doc/packages/mcelog

all: mcelog dbquery

.PHONY: install clean depend

OBJ := p4.o k8.o mcelog.o dmi.o db.o dimm.o tsc.o core2.o bitfield.o intel.o \
       nehalem.o dunnington.o tulsa.o config.o memutil.o
SRC := $(OBJ:.o=.c)
CLEAN := mcelog dmi tsc dbquery .depend .depend.X dbquery.o
DOC := mce.pdf smbios.spec

mcelog: ${OBJ}

# dbquery intentionally not installed by default
install: mcelog
	cp mcelog ${prefix}/sbin/mcelog
	cp mcelog.8 ${prefix}/share/man/man8
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

dbquery: db.o dbquery.o

depend: .depend

.depend: ${SRC}
	${CC} -MM -I. ${SRC} > .depend.X && mv .depend.X .depend

include .depend

Makefile: .depend
