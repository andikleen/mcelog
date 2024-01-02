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
#define _GNU_SOURCE 1
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "memutil.h"
#include "mcelog.h"
#include "tsc.h"
#include "intel.h"

static unsigned scale(u64 *tsc, unsigned unit, double mhz)
{
	u64 v = (u64)(mhz * 1000000) * unit;
	unsigned u = *tsc / v;
	*tsc -= u * v;
	return u;
}

static int fmt_tsc(char **buf, u64 tsc, double mhz)
{
	unsigned days, hours, mins, secs;
	if (mhz == 0.0)
		return -1;
	days = scale(&tsc, 3600 * 24, mhz);
	hours = scale(&tsc, 3600, mhz);
	mins = scale(&tsc, 60, mhz);
	secs = scale(&tsc, 1, mhz);
	xasprintf(buf, "[at %.0f Mhz %u days %u:%u:%u uptime (unreliable)]",
		mhz, days, hours, mins, secs);
	return 0;
}

static double cpufreq_mhz(int cpu, double infomhz)
{
	double mhz;
	FILE *f;
	char *fn;
	xasprintf(&fn, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
	f = fopen(fn, "r");
	free(fn);
	fn = NULL;
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
	mhz /= 1000;
	fclose(f);
	return mhz;
}

int decode_tsc_forced(char **buf, double mhz, u64 tsc)
{
	return fmt_tsc(buf, tsc, mhz);
}

static int deep_sleep_states(int cpu)
{
	int ret;
	char *fn;
	FILE *f;
	char *line = NULL;
	size_t linelen = 0;

	/* When cpuidle is active assume there are deep sleep states */
	xasprintf(&fn, "/sys/devices/system/cpu/cpu%d/cpuidle", cpu);
	ret = access(fn, X_OK);
	free(fn);
	fn = NULL;
	if (ret == 0)
		return 1;

	xasprintf(&fn, "/proc/acpi/processor/CPU%d/power", cpu);
	f = fopen(fn, "r");
	free(fn);
	fn = NULL;
	if (!f)
		return 0;

	while ((getline(&line, &linelen, f)) > 0) {
		int n;
		if ((sscanf(line, " C%d:", &n)) == 1) {
			if (n > 1) {
				char *p = strstr(line, "usage");
				if (p && sscanf(p, "usage[%d]", &n) == 1 && n > 0)
					return 1;					
			}
		}
	}
	free(line);
	line = NULL;
	fclose(f);
	return 0;
}

/* Try to figure out if this CPU has a somewhat reliable TSC clock */
static int tsc_reliable(int cputype, int cpunum)
{
	if (!processor_flags)
		return 0;
	/* Trust the kernel */
	if (strstr(processor_flags, "nonstop_tsc"))
		return 1;
	/* TSC does not change frequency TBD: really old kernels don't set that */
	if (!strstr(processor_flags, "constant_tsc"))
		return 0;	
	/* We don't know the frequency on non Intel CPUs because the
	   kernel doesn't report them (e.g. AMD GH TSC doesn't run at highest
	   P-state). But then the kernel can just report the real time too. 
	   Also a lot of AMD and VIA CPUs have unreliable TSC, so would
	   need special rules here too. */
	if (!is_intel_cpu(cputype))
		return 0;
	if (deep_sleep_states(cpunum) && cputype < CPU_NEHALEM)
		return 0;
	return 1;
}

int decode_tsc_current(char **buf, int cpunum, enum cputype cputype, double mhz, 
		       unsigned long long tsc)
{
	double cmhz;
	if (!tsc_reliable(cputype, cpunum))
		return -1;
	cmhz = cpufreq_mhz(cpunum, mhz);
	if (cmhz != 0.0)
		mhz = cmhz;
	return fmt_tsc(buf, tsc, mhz);
}

#ifdef STANDALONE
int is_intel_cpu(int cpu) { return 1; }
/* claim this TSC is reliable always */
char *processor_flags = "nonstop_tsc";

static inline u64 rdtscll(void)
{
	unsigned a,b;
	asm volatile("rdtsc" : "=a" (a), "=d" (b));
	return (u64)a | (((u64)b) << 32);
}

int main(void)
{
	char *buf;
	u64 tsc = rdtscll();
	printf("%llx tsc\n", tsc);
	if (decode_tsc_current(&buf, 0, CPU_CORE2, 0.0, tsc) >= 0)
		printf("%s\n", buf);
	else
		printf("failed\n");
	return 0;
}
#endif
