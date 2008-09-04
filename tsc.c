/* Copyright (C) 2006 Andi Kleen, SuSE Labs.
   Decode TSC value into human readable uptime

   mcelog is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version
   2.

   dmi is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should find a copy of v2 of the GNU General Public License somewhere
   on your Linux system; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
/* TBD:
   Implement support for CPUs where the TSC runs at a lower constant
   frequency. */
#define _GNU_SOURCE 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mcelog.h"

static int constant_tsc(int cpu, double *mhz)
{
	int found;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/cpuinfo","r");
	if (!f)
		return 0;
	found = 0;
	while (getline(&line, &linelen, f) > 0) {
		unsigned fcpu;
		if (!found) {
			if (sscanf(line, "processor : %u", &fcpu) == 1 &&
			    cpu == fcpu)
				found++;
			else
				continue;
		}
		if (sscanf(line, "cpu MHz : %lf", mhz) == 1)
			found++;
		else if (!strncmp(line, "vendor_id", 9)) {
			/* We don't know at which frequency non Intel CPUs
			   with constant_tsc run, so bail out */
			if (!strstr(line, "GenuineIntel"))
				return -1;
			found++;
		} else if (!strncmp(line, "flags", 5) &&
			 strstr(line,"constant_tsc"))
			found++;
		else if (!strncmp(line, "processor", 9))
			break;
	}
	free(line);
	fclose(f);
	return found == 4;
}

static int fmt_tsc(char *buf, __u64 tsc, double mhz)
{
	unsigned days, hours, mins, secs;
	__u64 v;
	if (mhz == 0.0)
		return -1;
#define SCALE(var, f) v = mhz * 1000000 * f; var = tsc / v; tsc -= v
	SCALE(days, 3600 * 24);
	SCALE(hours, 3600);
	SCALE(mins, 60);
	SCALE(days, 1);
	sprintf(buf, "%u days %u:%u:%u", days, hours, mins, secs);
	return 0;
}

static double cpufreq_mhz(int cpu, double infomhz)
{
	double mhz;
	FILE *f;
	char buf[200];
	sprintf(buf, "/sys/devices/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
	f = fopen(buf, "r");
	if (!f) {
		/* /sys exists, but no cpufreq -- use value from cpuinfo */
		if (access("/sys/devices", F_OK) == 0)
			return infomhz;
		/* /sys not mounted. We don't know if cpufreq is active
		   or not, so must fallback */
		return 0.0;
	}
	if (fscanf(f, "%lf", &mhz) != 1)
		mhz = 0.0;
	fclose(f);
	return mhz;
}

int decode_tsc_forced(char *buf, int cpu_type, double mhz, __u64 tsc)
{
	if (cpu_type != CPU_P4 && cpu_type != CPU_CORE2)
		return -1;
	return fmt_tsc(buf, tsc, mhz);
}

int decode_tsc_current(char *buf, int cpu, __u64 tsc)
{
	double infomhz;
	if (!constant_tsc(cpu, &infomhz))
		return -1;
	return fmt_tsc(buf, tsc, cpufreq_mhz(cpu, infomhz));
}

#ifdef STANDALONE
#include <asm/msr.h>
int main(void)
{
	char buf[200];
	unsigned long tsc;
	rdtscll(tsc);
	if (decode_tsc_current(buf, 0, tsc) >= 0)
		printf("%s\n", buf);
	else
		printf("failed\n");
	return 0;
}
#endif
