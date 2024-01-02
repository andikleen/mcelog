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
#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>
#include "mcelog.h"
#include "sysfs.h"
#include "memutil.h"

char *read_field(char *base, char *name)
{
	char *fn, *val;
	int n, fd;
	char *s;
	char *buf = xalloc(4096);

	xasprintf(&fn, "%s/%s", base, name);
	fd = open(fn, O_RDONLY);
	free(fn);
	fn = NULL;
	if (fd < 0)
		goto bad_buf;
	n = read(fd, buf, 4096);
	close(fd);
	if (n < 0)
		goto  bad_buf;
	val = xalloc(n + 1);
	memcpy(val, buf, n);
	val[n] = 0;
	free(buf);
	buf = NULL;
	s = memchr(val, '\n', n);
	if (s)
		*s = 0;
	return val;

bad_buf:
	free(buf);
	buf = NULL;
	SYSERRprintf("Cannot read sysfs field %s/%s", base, name);
	return xstrdup("");
}

unsigned read_field_num(char *base, char *name)
{
	unsigned num;
	char *val = read_field(base, name);
	int n = sscanf(val, "%u", &num);
	free(val);
	val = NULL;
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
	if (map->name) {
		free(val);
		val = NULL;
		return map->value;
	}
	Eprintf("sysfs field %s/%s has unknown string value `%s'\n", base, name, val);
	free(val);
	val = NULL;
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
	n = xvasprintf(&buf, fmt, ap);
	va_end(ap);
	n = write(fd, buf, n);
	e = errno;
	close(fd);
	free(buf);
	buf = NULL;
	errno = e;
	return n;
}

int sysfs_available(const char *name, int flags)
{
	return access(name, flags) == 0;
}
