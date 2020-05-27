/* Copyright (C) 2009 Intel Corporation 
   Author: Andi Kleen
   Handle 'yellow bit' cache error threshold indications.

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
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "memutil.h"
#include "mcelog.h"
#include "config.h"
#include "trigger.h"
#include "yellow.h"
#include "cache.h"

#define BITS_PER_U (sizeof(unsigned) * 8)
#define test_bit(i, a) (((unsigned *)(a))[(i) / BITS_PER_U] & (1U << ((i) % BITS_PER_U)))

static char *yellow_trigger;
static int yellow_log = 1;

enum {
	MAX_ENV = 10,
};

static char *cpulist(char *prefix, unsigned *cpumask, unsigned cpumasklen)
{
	unsigned i, k;
	char *buf = NULL;
	size_t size = 0;
	FILE *f = open_memstream(&buf, &size);
	if (!f)
		Enomem();
	fprintf(f, "%s", prefix);
	k = 0;
	for (i = 0; i < cpumasklen * 8; i++) {
		if (test_bit(i, cpumask)) {
			fprintf(f, "%s%u", k > 0 ? " " : "", i);
			k++;
		}
	}
	fclose(f);
	return buf;
}

void run_yellow_trigger(int cpu, int tnum, int lnum, char *ts, char *ls, int socket)
{
	int ei = 0;
	char *env[MAX_ENV];
	unsigned *cpumask;
	int cpumasklen;
	int i;
	char *msg;
	char *location;

	if (socket >= 0) 
		xasprintf(&location, "CPU %d on socket %d", cpu, socket);
	else
		xasprintf(&location, "CPU %d", cpu);
	xasprintf(&msg, "%s has large number of corrected cache errors in %s %s",
		location, ls, ts);
	free(location);
	if (yellow_log) {
		Lprintf("%s\n", msg);
		Lprintf("System operating correctly, but might lead to uncorrected cache errors soon\n");
	}
	if (!yellow_trigger)
		goto out;

	if (socket >= 0)
		xasprintf(&env[ei++], "SOCKETID=%d", socket);
	xasprintf(&env[ei++], "MESSAGE=%s", msg);
	xasprintf(&env[ei++], "CPU=%d", cpu);
	xasprintf(&env[ei++], "LEVEL=%d", lnum);
	xasprintf(&env[ei++], "TYPE=%s", ts);
	if (cache_to_cpus(cpu, lnum, tnum, &cpumasklen, &cpumask) >= 0)
		env[ei++] = cpulist("AFFECTED_CPUS=", cpumask, cpumasklen); 
	else
		xasprintf(&env[ei++], "AFFECTED_CPUS=unknown");
	env[ei] = NULL;	
	assert(ei < MAX_ENV);

	run_trigger(yellow_trigger, NULL, env, false, "yellow");
	for (i = 0; i < ei; i++)
		free(env[i]);
out:
	free(msg);
}

void yellow_setup(void)
{
	int n;

	yellow_trigger = config_string("cache", "cache-threshold-trigger"); 
	if (yellow_trigger && trigger_check(yellow_trigger) < 0) {
		SYSERRprintf("Cannot access cache threshold trigger `%s'", 
				yellow_trigger);
		exit(1);
	}

	n = config_bool("cache", "cache-threshold-log");
	if (n >= 0)
		yellow_log = n;
}

