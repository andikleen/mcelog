/* Copyright (C) 2008 Intel Corporation 
   Author: Andi Kleen
   Parse sysfs exported CPU cache topology

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
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <assert.h>
#include <ctype.h>
#include "mcelog.h"
#include "memutil.h"
#include "sysfs.h"
#include "cache.h"

struct cache { 
	unsigned level;
	/* Numerical values must match MCACOD */
	enum { INSTR, DATA, UNIFIED } type; 
	unsigned *cpumap;
	unsigned cpumaplen;
};

struct cache **caches;
static unsigned cachelen;

#define PREFIX "/sys/devices/system/cpu"
#define MIN_CPUS 8
#define MIN_INDEX 4

static struct map type_map[] = {
	{ "Instruction", INSTR },
	{ "Data", DATA },
	{ "Unified", UNIFIED },
	{ },
};

static void more_cpus(unsigned cpu)
{
	int old = cachelen;
	if (!cachelen)
		cachelen = MIN_CPUS/2;	
	if (cachelen < cpu)
		cachelen = cpu + 1;
	cachelen = cachelen * 2;
	caches = xrealloc(caches, cachelen * sizeof(struct cache *));
	memset(caches + old, 0, (cachelen - old) * sizeof(struct cache *));
}

static unsigned cpumap_len(char *s)
{
	unsigned len = 0, width = 0;
	do {
		if (isxdigit(*s))
			width++;
		else {
			len += round_up(width * 4, BITS_PER_INT) / 8;
			width = 0;
		}
	} while (*s++);
	return len;
}

static void parse_cpumap(char *map, unsigned *buf, unsigned len)
{
	char *s;
	int c;

	c = 0;
	s = map + strlen(map);
	for (;;) { 
		s = memrchr(map, ',', s - map);
		if (!s) 
			s = map;
		else
			s++;
		buf[c++] = strtoul(s, NULL, 16);
		if (s == map)
			break;
		s--;
	}
	assert(len == c * sizeof(unsigned));
}

static void read_cpu_map(struct cache *c, char *cfn)
{
	char *map = read_field(cfn, "shared_cpu_map");
	if (map[0] == 0) {
		c->cpumap = NULL;
		goto out;
	}
	c->cpumaplen = cpumap_len(map);
	c->cpumap = xalloc(c->cpumaplen);
	parse_cpumap(map, c->cpumap, c->cpumaplen);
out:
	free(map);
}

static int read_caches(void)
{
	DIR *cpus = opendir(PREFIX);
	struct dirent *de;
	if (!cpus) { 
		Wprintf("Cannot read cache topology from %s", PREFIX);
		return -1;
	}
	while ((de = readdir(cpus)) != NULL) {
		unsigned cpu;
		if (sscanf(de->d_name, "cpu%u", &cpu) == 1) { 
			struct stat st;
			char *fn;
			int i;
			int numindex;

			xasprintf(&fn, "%s/%s/cache", PREFIX, de->d_name);
			if (!stat(fn, &st)) {
				numindex = st.st_nlink - 2;
				if (numindex < 0)
					numindex = MIN_INDEX;
				if (cpu >= cachelen)
					more_cpus(cpu);
				assert(cpu < cachelen);
				caches[cpu] = xalloc(sizeof(struct cache) * 
						     (numindex+1));
				for (i = 0; i < numindex; i++) {
					char *cfn;
					struct cache *c = caches[cpu] + i;
					xasprintf(&cfn, "%s/index%d", fn, i);
					c->type = read_field_map(cfn, "type", type_map);
					c->level = read_field_num(cfn, "level");
					read_cpu_map(c, cfn);
					free(cfn);
				}
			}
			free(fn);
		}
	}
	closedir(cpus);
	return 0;
}

int cache_to_cpus(int cpu, unsigned level, unsigned type, 
		   int *cpulen, unsigned **cpumap)
{
	struct cache *c;
	if (!caches) {
		if (read_caches() < 0)
			return -1;
		if (!caches) { 
			Wprintf("No caches found in sysfs");
			return -1;
		}
	}
	for (c = caches[cpu]; c && c->cpumap; c++) { 
		//printf("%d level %d type %d\n", cpu, c->level, c->type);
		if (c->level == level && (c->type == type || c->type == UNIFIED)) {
			*cpumap = c->cpumap;
			*cpulen = c->cpumaplen;
			return 0;
		}
	}
	Wprintf("Cannot find sysfs cache for CPU %d", cpu);
	return -1;
}

#ifdef TEST
main()
{
	int cpulen;
	unsigned *cpumap;
	cache_to_cpus(1, 1, INSTR, &cpulen, &cpumap); printf("%d %x\n", cpulen, cpumap[0]);
	cache_to_cpus(1, 1, DATA, &cpulen, &cpumap); printf("%d %x\n", cpulen, cpumap[0]);
	cache_to_cpus(1, 2, UNIFIED, &cpulen, &cpumap); printf("%d %x\n", cpulen, cpumap[0]);
	cache_to_cpus(0, 1, INSTR, &cpulen, &cpumap); printf("%d %x\n", cpulen, cpumap[0]);
	cache_to_cpus(0, 1, DATA, &cpulen, &cpumap); printf("%d %x\n", cpulen, cpumap[0]);
	cache_to_cpus(0, 2, UNIFIED, &cpulen, &cpumap); printf("%d %x\n", cpulen, cpumap[0]);
}
#endif

