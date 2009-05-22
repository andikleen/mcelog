/* Copyright (C) 2008 Intel Corporation 
   Author: Andi Kleen
   Memory allocation utilities

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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "mcelog.h"
#include "memutil.h"

void Enomem(void)
{
	Eprintf("out of memory");
	exit(ENOMEM);
}

void *xalloc(size_t size)
{
	void *m = calloc(1, size);
	if (!m)
		Enomem();
	return m;
}

void *xrealloc(void *old, size_t size)
{
	void *m = realloc(old, size);
	if (!m)
		Enomem();
	return m;
}

char *xstrdup(char *str)
{
	str = strdup(str);
	if (!str)
		Enomem();
	return str;
}
