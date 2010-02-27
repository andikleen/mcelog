/* Copyright (C) 2008 Intel Corporation 
   Author: Andi Kleen
   Read/Write sysfs values

   mcelog is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version
   2.

   mcelog is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should find a copy of v2 of the GNU General Public License somewhere
   on your Linux system; if not, write to the Free Software Foundation, 
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include "mcelog.h"
#include "sysfs.h"
#include "memutil.h"

char *read_field(char *base, char *name)
{
	char *fn, *val;
	int n, fd;
	struct stat st;
	char *s;
	char *buf = NULL;

	asprintf(&fn, "%s/%s", base, name);
	fd = open(fn, O_RDONLY);
	if (fstat(fd, &st) < 0)
		goto bad;		
	buf = xalloc(st.st_size);
	free(fn);
	if (fd < 0) 
		goto bad;
	n = read(fd, buf, st.st_size);
	close(fd);
	if (n < 0)
		goto  bad;
	val = xalloc(n);
	memcpy(val, buf, n);
	free(buf);
	s = strchr(val, '\n');
	if (s)
		*s = 0;
	return  val;

bad:
	SYSERRprintf("Cannot read sysfs field %s/%s", base, name);
	return xstrdup("");
}

unsigned read_field_num(char *base, char *name)
{
	unsigned num;
	char *val = read_field(base, name);
	int n = sscanf(val, "%u", &num);
	free(val);
	if (n != 1) { 
		Eprintf("Cannot parse number in sysfs field %s/%s\n", base,name);
		return 0;
	}
	return num;
}

unsigned read_field_map(char *base, char *name, struct map *map)
{
	char *val = read_field(base, name);

	for (; map->name; map++) {
		if (!strcmp(val, map->name))
			break;
	}
	free(val);
	if (map->name)
		return map->value;
	Eprintf("sysfs field %s/%s has unknown string value `%s'\n", base, name, val);
	return -1;
}

int sysfs_write(const char *name, const char *fmt, ...)
{
	int e;
	int n;
	char *buf;
	va_list ap;
	int fd = open(name, O_WRONLY);
	if (fd < 0)
		return -1;
	va_start(ap, fmt);
	n = vasprintf(&buf, fmt, ap);
	va_end(ap);
	n = write(fd, buf, n);
	e = errno;
	close(fd);
	free(buf);
	errno = e;
	return n;
}

int sysfs_available(const char *name, int flags)
{
	return access(name, flags) == 0;
}
