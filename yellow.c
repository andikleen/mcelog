/* Handle 'yellow bit' cache error threshold indications. */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "memutil.h"
#include "mcelog.h"
#include "config.h"
#include "trigger.h"
#include "yellow.h"
#include "cache.h"

#define BITS_PER_U (sizeof(unsigned) * 8)
#define test_bit(i, a) (((unsigned *)(a))[(i) / BITS_PER_U] & (1U << ((i) % BITS_PER_U)))

static char *yellow_trigger;

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

void run_yellow_trigger(int cpu, int tnum, int lnum, char *ts, char *ls)
{
	int ei = 0;
	char *env[MAX_ENV];
	unsigned *cpumask;
	int cpumasklen;
	int i;

	// xxx print socket, but need to figure out if it's valid
	Lprintf("CPU %d has large number of corrected cache errors in %s %s\n", 
		cpu, ls, ts);
	if (!yellow_trigger)
		return;

	// xxx socketid
	// xxx more stuff?
	asprintf(&env[ei++], "CPU=%d", cpu);
	asprintf(&env[ei++], "LEVEL=%d", lnum);
	asprintf(&env[ei++], "TYPE=%s", ts);
	if (cache_to_cpus(cpu, lnum, tnum, &cpumasklen, &cpumask) >= 0)
		env[ei++] = cpulist("AFFECTED_CPUS=", cpumask, cpumasklen); 
	env[ei] = NULL;	
	assert(ei < MAX_ENV);

	run_trigger(yellow_trigger, NULL, env);
	for (i = 0; i < ei; i++)
		free(env[i]);
}

void yellow_setup(void)
{
	yellow_trigger = config_string("cache", "yellow-bit-trigger"); 
}

