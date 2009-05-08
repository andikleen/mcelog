/* Copyright (C) 2004,2005,2006 Andi Kleen, SuSE Labs.
   Copyright (C) 2008 Intel Corporation 
   Authors: Andi Kleen, Ying Huang
   Decode IA32/x86-64 machine check events in /dev/mcelog. 

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
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <asm/ioctls.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <ctype.h>
#include <poll.h>
#include <time.h>
#include <getopt.h>
#include "mcelog.h"
#include "paths.h"
#include "k8.h"
#include "intel.h"
#include "p4.h"
#include "dmi.h"
#include "dimm.h"
#include "tsc.h"
#include "version.h"
#include "config.h"
#include "diskdb.h"

enum cputype cputype = CPU_GENERIC;	

char *logfn = LOG_DEV_FILENAME; 

enum { 
	SYSLOG_LOG = (1 << 0),
	SYSLOG_REMARK = (1 << 1), 
	SYSLOG_ERROR  = (1 << 2),
	SYSLOG_ALL = SYSLOG_LOG|SYSLOG_REMARK|SYSLOG_ERROR,
	SYSLOG_FORCE = (1 << 3),
} syslog_opt = SYSLOG_REMARK;
int syslog_level = LOG_WARNING;
int ignore_nodev;
int filter_bogus;
int cpu_forced;
double cpumhz;
int ascii_mode;
int dump_raw_ascii;
int daemon_mode;

static void check_cpu(void);

static void opensyslog(void)
{
	static int syslog_opened;
	if (syslog_opened)
		return;
	syslog_opened = 1;
	openlog("mcelog", 0, 0);
}

/* For warning messages that should reach syslog */
void Lprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (syslog_opt & SYSLOG_REMARK) { 
		opensyslog();
		vsyslog(LOG_ERR, fmt, ap);
	} else { 
		vprintf(fmt, ap);
	}
	va_end(ap);
}

/* For errors during operation */
void Eprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (!(syslog_opt & SYSLOG_ERROR)) {
		fputs("mcelog: ", stderr);
		vfprintf(stderr, fmt, ap);
		fputc('\n', stderr);
	} else { 
		opensyslog();
		vsyslog(LOG_ERR, fmt, ap);
	}
	va_end(ap);
}

/* Write to syslog with line buffering */
static void vlinesyslog(char *fmt, va_list ap)
{
	static char line[200];
	int n;
	int lend = strlen(line); 
	vsnprintf(line + lend, sizeof(line)-lend, fmt, ap);
	while (line[n = strcspn(line, "\n")] != 0) {
		line[n] = 0;
		syslog(syslog_level, line);
		memmove(line, line + n + 1, strlen(line + n + 1) + 1);
	}	
}

/* For decoded machine check output */
void Wprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);
	if (syslog_opt & SYSLOG_ERROR) {
		opensyslog();
		vlinesyslog(fmt, ap);
	} else {
		vprintf(fmt, ap);
	}
	va_end(ap);
}

/* For output that should reach both syslog and normal log */
void Gprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);
	if (syslog_opt & SYSLOG_LOG) {
		vlinesyslog(fmt, ap);
	} else if (syslog_opt & SYSLOG_REMARK) { 
		vprintf(fmt, ap);
		vlinesyslog(fmt, ap);
	} else { 
		vprintf(fmt, ap);
	}
	va_end(ap);
}

char *extended_bankname(unsigned bank) 
{
	static char buf[64];
	switch (bank) { 
	case MCE_THERMAL_BANK:
		return "THERMAL EVENT";
	case MCE_TIMEOUT_BANK:
		return "Timeout waiting for exception on other CPUs";
	case K8_MCE_THRESHOLD_BASE ... K8_MCE_THRESHOLD_TOP:
		return k8_bank_name(bank);

		/* add more extended banks here */

	default:
		sprintf(buf, "Undecoded extended event %x", bank);
		return buf;
	} 
}

char *bankname(unsigned bank) 
{ 
	static char numeric[64];
	if (bank >= MCE_EXTENDED_BANK) 
		return extended_bankname(bank);

	switch (cputype) { 
	case CPU_K8:
		return k8_bank_name(bank);
	CASE_INTEL_CPUS:
		return intel_bank_name(bank);
	/* add banks of other cpu types here */
	default:
		sprintf(numeric, "BANK %d", bank); 
		return numeric;
	}
} 

void resolveaddr(unsigned long addr)
{
	if (addr && do_dmi)
		dmi_decodeaddr(addr);
	/* Should check for PCI resources here too */
}

int mce_filter(struct mce *m)
{
	if (!filter_bogus) 
		return 1;
	/* Filter out known broken MCEs */
	switch (cputype) {
	case CPU_K8:
		return mce_filter_k8(m);
		/* add more buggy CPUs here */
	CASE_INTEL_CPUS:
		/* No bugs known */
		return 1;
	default:
	case CPU_GENERIC:
		return 1;
	}	
}

static void print_tsc(int cpunum, __u64 tsc) 
{ 
	char buf[200];
	int ret; 
	if (cpu_forced) 
		ret = decode_tsc_forced(buf, cputype, cpumhz, tsc);
	else
		ret = decode_tsc_current(buf, cputype, tsc);
	if (ret >= 0) 
		Wprintf(buf);
	else
		Wprintf("%Lx", tsc);
}

struct cpuid1 {
	unsigned stepping : 4;
	unsigned model : 4;
	unsigned family : 4;
	unsigned type : 2;
	unsigned res1 : 2;
	unsigned ext_model : 4;
	unsigned ext_family : 8; 
	unsigned res2 : 4;
};

static void parse_cpuid(u32 cpuid, u32 *family, u32 *model)
{
	union { 
		struct cpuid1 c;
		u32 v;
	} c;

	/* Algorithm from IA32 SDM 2a 3-191 */
	c.v = cpuid;
	*family = c.c.family; 
	if (*family == 0xf) 
		*family += c.c.ext_family;
	*model = c.c.model;
	if (*family == 6 || *family == 0xf) 
		*model += c.c.ext_model << 4;
}

static char *cputype_name[] = {
	[CPU_GENERIC] = "generic CPU",
	[CPU_P6OLD] = "Intel PPro/P2/P3/old Xeon",
	[CPU_CORE2] = "Intel Core", /* 65nm and 45nm */
	[CPU_K8] = "AMD K8 and derivates",
	[CPU_P4] = "Intel P4",
	[CPU_NEHALEM] = "Intel Xeon 75xx / Core i7 (\"Nehalem\")",
	[CPU_DUNNINGTON] = "Intel Xeon 7400",
	[CPU_TULSA] = "Intel Xeon 71xx",
};

static char *cpuvendor_name(u32 cpuvendor)
{
	static char *vendor[] = {
		[0] = "Intel",
		[1] = "Cyrix",
		[2] = "AMD",
		[3] = "UMC", 
		[4] = "vendor 4",
		[5] = "Centaur",
		[6] = "vendor 6",
		[7] = "Transmeta",
		[8] = "NSC"
	};
	return (cpuvendor < NELE(vendor)) ? vendor[cpuvendor] : "Unknown vendor";
}

static enum cputype setup_cpuid(u32 cpuvendor, u32 cpuid)
{
	u32 family, model;

	parse_cpuid(cpuid, &family, &model);

	switch (cpuvendor) { 
	case X86_VENDOR_INTEL:
	        return select_intel_cputype(family, model);
	case X86_VENDOR_AMD:
		if (family >= 15 && family <= 17)
			return CPU_K8;
		/* FALL THROUGH */
	default:
		Eprintf("Unknown CPU type vendor %u family %x model %x", 
			cpuvendor, family, model);
		return CPU_GENERIC;
	}
}

static void mce_cpuid(struct mce *m)
{
	if (m->cpuid) {
		enum cputype t = setup_cpuid(m->cpuvendor, m->cpuid);
		if (!cpu_forced)
			cputype = t;
		else if (t != cputype && t != CPU_GENERIC)
			Eprintf("Forced cputype %s does not match cpu type %s from mcelog",
				cputype_name[cputype],
				cputype_name[t]);
	} else if (cputype == CPU_GENERIC && !cpu_forced) { 
		check_cpu();
	}	
}

static void print_time(time_t t)
{
	if (t)
		Wprintf("%s", ctime(&t));
}

void dump_mce(struct mce *m) 
{
	int ismemerr = 0;
	unsigned cpu = m->extcpu ? m->extcpu : m->cpu;

	mce_cpuid(m);
	print_time(m->time);
	Wprintf("HARDWARE ERROR. This is *NOT* a software problem!\n");
	Wprintf("Please contact your hardware vendor\n");
	/* should not happen */
	if (!m->finished)
		Wprintf("not finished?\n");
	Wprintf("CPU %d %s ", cpu, bankname(m->bank));
	if (m->tsc) {
		Wprintf("TSC ");
		print_tsc(cpu, m->tsc);
		if (m->mcgstatus & MCI_STATUS_UC)
			Wprintf(" (upper bound, found by polled driver)");
		Wprintf("\n");
	}
	if (m->rip) 
		Wprintf("RIP%s %02x:%Lx ", 
		       !(m->mcgstatus & MCG_STATUS_EIPV) ? " !INEXACT!" : "",
		       m->cs, m->rip);
	if (m->misc)
		Wprintf("MISC %Lx ", m->misc);
	if (m->addr)
		Wprintf("ADDR %Lx ", m->addr);
	if (m->rip | m->misc | m->addr)	
		Wprintf("\n");
	switch (cputype) { 
	case CPU_K8:
		ismemerr = decode_k8_mc(m); 
		break;
	CASE_INTEL_CPUS:
		decode_intel_mc(m, cputype);
		break;
	/* add handlers for other CPUs here */
	default:
		break;
	} 
	/* decode all status bits here */
	Wprintf("STATUS %Lx MCGSTATUS %Lx\n", m->status, m->mcgstatus);
	if (m->cpuid) {
		u32 fam, mod;
		parse_cpuid(m->cpuid, &fam, &mod);
		Wprintf("CPUID Vendor %s Family %u Model %u\n",
			cpuvendor_name(m->cpuvendor), 
			fam,
			mod);
	}
	resolveaddr(m->addr);
	if (!ascii_mode && ismemerr) {
		diskdb_resolve_addr(m->addr);
	}
}

void dump_mce_raw_ascii(struct mce *m)
{
	/* should not happen */
	if (!m->finished)
		Wprintf("not finished?\n");
	Wprintf("CPU %u\n", m->extcpu ? m->extcpu : m->cpu);
	Wprintf("BANK %d\n", m->bank);
	Wprintf("TSC 0x%Lx\n", m->tsc);
	Wprintf("RIP 0x%02x:0x%Lx\n", m->cs, m->rip);
	Wprintf("MISC 0x%Lx\n", m->misc);
	Wprintf("ADDR 0x%Lx\n", m->addr);
	Wprintf("STATUS 0x%Lx\n", m->status);
	Wprintf("MCGSTATUS 0x%Lx\n", m->mcgstatus);
	if (m->cpuid)
		Wprintf("PROCESSOR %u:0x%x\n", m->cpuvendor, m->cpuid);
	if (m->time)
		Wprintf("TIME %Lu\n", m->time);
	Wprintf("\n");
}

static void check_cpu(void)
{ 
	FILE *f;
	f = fopen("/proc/cpuinfo","r");
	if (f != NULL) { 
		int found = 0; 
		int family; 
		int model;
		char vendor[64];
		char *line = NULL;
		size_t linelen = 0; 
		while (getdelim(&line, &linelen, '\n', f) > 0 && found < 3) { 
			if (sscanf(line, "vendor_id : %63[^\n]", vendor) == 1) 
				found++; 
			if (sscanf(line, "cpu family : %d", &family) == 1)
				found++;
			if (sscanf(line, "model : %d", &model) == 1)
				found++;
		} 
		if (found == 3) {
			if (!strcmp(vendor,"AuthenticAMD") && 
			    ( family == 15 || family == 16 || family == 17) )
				cputype = CPU_K8;
			if (!strcmp(vendor,"GenuineIntel"))
				cputype = select_intel_cputype(family, model);
			/* Add checks for other CPUs here */	
		} else {
			fprintf(stderr, 
			"mcelog: warning: Cannot parse /proc/cpuinfo\n"); 
		} 
		fclose(f);
		free(line);
	} else
		fprintf(stderr, "mcelog: warning: Cannot open /proc/cpuinfo\n");
} 

char *skipspace(char *s)
{
	while (isspace(*s))
		++s;
	return s;
}

static char *skipgunk(char *s)
{
	s = skipspace(s);
	if (*s == '<') { 
		s += strcspn(s, ">"); 
		if (*s == '>') 
			++s; 
	}
	s = skipspace(s);
	if (*s == '[') {
		s += strcspn(s, "]");
		if (*s == ']')
			++s;
	}
	return skipspace(s);
}

void dump_mce_final(struct mce *m, char *symbol, int missing)
{
	m->finished = 1;
	if (!dump_raw_ascii) {
		dump_mce(m);
		if (symbol[0])
			Wprintf("RIP: %s\n", symbol);
		if (missing) 
			Wprintf("(Fields were incomplete)\n");
	} else
		dump_mce_raw_ascii(m);
}

/* Decode ASCII input for fatal messages */
void decodefatal(FILE *inf)
{
	struct mce m;
	char *line = NULL; 
	size_t linelen = 0;
	int k;
	int missing = 0; 
	char symbol[100];
	int data = 0;
	int next = 0;
	char *s = NULL;
	unsigned cpuvendor;

	ascii_mode = 1;
	if (do_dmi)
		Wprintf(
 "WARNING: with --dmi mcelog --ascii must run on the same machine with the\n"
 "     same BIOS/memory configuration as where the machine check occurred.\n");

	k = 0;
	memset(&m, 0, sizeof(struct mce));
	symbol[0] = '\0';
	while (next > 0 || getdelim(&line, &linelen, '\n', inf) > 0) { 
		int n = 0;

		s = next > 0 ? s + next : line;
		s = skipgunk(s);
		next = 0;

		if (!strncmp(s, "CPU", 3)) { 
			unsigned cpu = 0, bank = 0;
			n = sscanf(s,
	       "CPU %u: Machine Check Exception: %16Lx Bank %d: %016Lx%n",
				   &cpu,
				   &m.mcgstatus,
				   &bank,
				   &m.status,
				   &next);
			if (n == 1) {
				n = sscanf(s, "CPU %u BANK %u%n", &cpu, &bank, 
						&next);
				if (n != 2)
					n = sscanf(s, "CPU %u %u%n", &cpu,
						 &bank, &next);
				m.cpu = cpu;
				if (n < 2) 
					missing++;
				else
					m.bank = bank;
			} else { 
				m.cpu = cpu;
				m.bank = bank;
				if (n < 4) 
					missing++; 

			}
		} 
		else if (!strncmp(s, "STATUS", 6)) {
			if ((n = sscanf(s,"STATUS %Lx%n", &m.status, &next)) < 1)
				missing++;
		}
		else if (!strncmp(s, "MCGSTATUS", 6)) {
			if ((n = sscanf(s,"MCGSTATUS %Lx%n", &m.mcgstatus, &next)) < 1)
				missing++;
		}
		else if (!strncmp(s, "RIP", 3)) { 
			unsigned cs = 0; 

			if (!strncmp(s, "RIP !INEXACT!", 13))
				s += 13; 
			else
				s += 3; 

			n = sscanf(s, "%02x:<%016Lx> {%100s}%n",
				   &cs,
				   &m.rip, 
				   symbol, &next); 
			m.cs = cs;
			if (n < 2) 
				missing++; 
		} 
		else if (!strncmp(s, "TSC",3)) { 
			if ((n = sscanf(s, "TSC %Lx%n", &m.tsc, &next)) < 1) 
				missing++; 
		}
		else if (!strncmp(s, "ADDR",4)) { 
			if ((n = sscanf(s, "ADDR %Lx%n", &m.addr, &next)) < 1) 
				missing++;
		}
		else if (!strncmp(s, "MISC",4)) { 
			if ((n = sscanf(s, "MISC %Lx%n", &m.misc, &next)) < 1) 
				missing++; 
		} else if (!strncmp(s, "PROCESSOR", 9)) { 
			if ((n = sscanf(s, "PROCESSOR %u:%x%n", &cpuvendor, &m.cpuid, &next)) < 2)
				missing++;
			else
				m.cpuvendor = cpuvendor;			
		}
		else if (!strncmp(s, "TIME", 4)) { 
			if ((n = sscanf(s, "TIME %Lu%n", &m.time, &next)) < 1)
				missing++;
		} else { 
			s = skipspace(s);
			if (*s && data) { 
				dump_mce_final(&m, symbol, missing); 
				data = 0;
			} 
			if (!dump_raw_ascii)
				Wprintf("%s", line);
		} 
		if (n > 0) 
			data = 1;
	} 
	free(line);
	if (data)
		dump_mce_final(&m, symbol, missing);
}

void usage(void)
{
	fprintf(stderr, 
"Usage:\n"
"  mcelog [options]  [--ignorenodev] [--dmi] [--syslog] [--filter] [mcelogdevice]\n"
"Decode machine check error records from kernel.\n"
"Normally this is invoked from a cronjob or using the kernel trigger.\n"
"  mcelog [options] [--dmi] --ascii < log\n"
"Decode machine check ASCII output from kernel logs\n"
"Manage memory error database\n"
"  mcelog [options] --drop-old-memory|--reset-memory locator\n"
"  mcelog --dump-memory locator\n"
"  old can be either locator or name\n"
"Options:\n"  
"--p4|--k8|--core2|--generic|--intel-cpu=family,model Set CPU type to decode\n"
"--cpumhz MHZ        Set CPU Mhz to decode\n"
"--raw		     (with --ascii) Dump in raw ASCII format for machine processing\n"
"--daemon            Run in background polling for events (needs newer kernel)\n"
"--syslog            Log decoded machine checks in syslog (default stdout)\n"	     
"--syslog-error	     Log decoded machine checks in syslog with error level\n"
"--no-syslog         Never log anything to syslog\n"
"--logfile=filename  Append log output to logfile instead of stdout\n"
"--config-file filename Read config information from config file instead of " CONFIG_FILENAME "\n"
		);
	diskdb_usage();
	exit(1);
}

enum options { 
	O_LOGFILE = O_COMMON, 
	O_K8,
	O_P4,
	O_GENERIC,
	O_CORE2,
	O_INTEL_CPU,
	O_FILTER,
	O_DMI,
	O_NO_DMI,
	O_DMI_VERBOSE,
	O_SYSLOG,
	O_NO_SYSLOG,
	O_CPUMHZ,
	O_SYSLOG_ERROR,
	O_RAW,
	O_DAEMON,
	O_ASCII,
	O_VERSION,
	O_CONFIG_FILE,
};

static struct option options[] = {
	{ "logfile", 1, NULL, O_LOGFILE },
	{ "k8", 0, NULL, O_K8 },
	{ "p4", 0, NULL, O_P4 },
	{ "generic", 0, NULL, O_GENERIC },
	{ "core2", 0, NULL, O_CORE2 },
	{ "intel-cpu", 1, NULL, O_INTEL_CPU },
	{ "ignorenodev", 0, &ignore_nodev, 1 },
	{ "filter", 0, &filter_bogus, 1 },
	{ "dmi", 0, NULL, O_DMI },
	{ "no-dmi", 0, NULL, O_NO_DMI },
	{ "dmi-verbose", 1, NULL, O_DMI_VERBOSE },
	{ "syslog", 0, NULL, O_SYSLOG },
	{ "cpumhz", 1, NULL, O_CPUMHZ },
	{ "database", 1, NULL, O_DATABASE },
	{ "error-trigger", 1, NULL, O_ERROR_TRIGGER },
	{ "syslog-error", 0, NULL, O_SYSLOG_ERROR },
	{ "dump-raw-ascii", 0, &dump_raw_ascii, 1 },
	{ "raw", 0, &dump_raw_ascii, 1 },
	{ "no-syslog", 0, NULL, O_NO_SYSLOG },
	{ "daemon", 0, NULL, O_DAEMON },
	{ "dump-memory", 2, NULL, O_DUMP_MEMORY },
	{ "reset-memory", 2, NULL, O_RESET_MEMORY },
	{ "drop-old-memory", 0, NULL, O_DROP_OLD_MEMORY },
	{ "ascii", 0, NULL, O_ASCII },
	{ "version", 0, NULL, O_VERSION },
	{ "config-file", 1, NULL, O_CONFIG_FILE },
	{}
};

static int modifier(int opt)
{
	int v;

	switch (opt) { 
	case O_LOGFILE:
		fclose(stdout);
		if (!freopen(optarg, "a", stdout)) {
			Eprintf("Cannot open log file %s. Exiting.", optarg);	
			exit(1);
		}
		break;
	case O_K8:
		cputype = CPU_K8;
		cpu_forced = 1;
		break;
	case O_P4:
		cputype = CPU_P4;
		cpu_forced = 1;
		break;
	case O_GENERIC:
		cputype = CPU_GENERIC;
		cpu_forced = 1;
		break;
	case O_CORE2:
		cputype = CPU_CORE2;
		cpu_forced = 1;
		break;
	case O_INTEL_CPU: { 
		unsigned fam, mod;
		if (sscanf(optarg, "%i,%i", &fam, &mod) != 2)
			usage();
		cputype = select_intel_cputype(fam, mod);
		if (cputype == CPU_GENERIC) {
			fprintf(stderr, "Unknown Intel CPU\n");
			usage();
		}
		cpu_forced = 1;
		break;
	}
	case O_DMI:
		do_dmi = 1;
		dmi_forced = 1;
		break;
	case O_NO_DMI:
		dmi_forced = 1;
		do_dmi = 0;
		break;
	case O_DMI_VERBOSE:
		if (sscanf(optarg, "%i", &v) != 1)
			usage();
		dmi_set_verbosity(v);
		break;
	case O_SYSLOG:
		openlog("mcelog", 0, LOG_DAEMON);
		syslog_opt = SYSLOG_ALL|SYSLOG_FORCE;
		break;
	case O_NO_SYSLOG:
		syslog_opt = SYSLOG_FORCE;
		break;
	case O_CPUMHZ:
		if (!cpu_forced) {
			fprintf(stderr, 
				"Specify cputype before --cpumhz=..\n");
			usage();
		}
		if (sscanf(optarg, "%lf", &cpumhz) != 1)
			usage();
		break;
	case O_SYSLOG_ERROR:
		syslog_level = LOG_ERR;
		syslog_opt = SYSLOG_ALL|SYSLOG_FORCE;
		break;
	case O_DAEMON:
		daemon_mode = 1;
		if (!(syslog_opt & SYSLOG_FORCE))
			syslog_opt = SYSLOG_ALL|SYSLOG_FORCE;
		break;
	case O_CONFIG_FILE:
		/* parsed in config.c */
		break;
	case 0:
		break;
	default:
		return 0;
	}
	return 1;
} 

void argsleft(int ac, char **av)
{
	int opt;
		
	while ((opt = getopt_long(ac, av, "", options, NULL)) != -1) { 
		if (modifier(opt) != 1)
			usage();
	}
}

void no_syslog(void)
{
	if (!(syslog_opt & SYSLOG_FORCE))
		syslog_opt = 0;
}

static int combined_modifier(int opt)
{
	int r = modifier(opt);
	if (r == 0)
		r = diskdb_modifier(opt);
	return r;
}

static void process(int fd, unsigned recordlen, unsigned loglen, char *buf)
{	
	int len = read(fd, buf, recordlen * loglen); 
	if (len < 0) 
		err("read"); 

	int i; 
	for (i = 0; i < len / recordlen; i++) { 
		struct mce *mce = (struct mce *)(buf + i*recordlen);
		if (!mce_filter(mce)) 
			continue;
		if (!dump_raw_ascii) {
			Wprintf("MCE %d\n", i);
			dump_mce(mce);
		} else
			dump_mce_raw_ascii(mce);
	}

	if (recordlen < sizeof(struct mce))  {
		Eprintf("warning: %lu bytes ignored in each record",
				(unsigned long)recordlen - sizeof(struct mce)); 
		Eprintf("consider an update"); 
	}
}

static void noargs(int ac, char **av)
{
	if (getopt_long(ac, av, "", options, NULL) != -1)
		usage();
}

static void parse_config(char **av)
{
	static const char config_fn[] = CONFIG_FILENAME;
	const char *fn = config_file(av, config_fn);
	if (!fn)
		usage();
	if (parse_config_file(fn) < 0) { 
		/* If it's the default file don't complain if it isn't there */
		if (fn != config_fn) {
			Eprintf("Cannot open config file %s\n", fn);
			exit(1);
		}
		return;
	}
	config_options(options, combined_modifier);
}

int main(int ac, char **av) 
{ 
	unsigned recordlen = 0;
	unsigned loglen = 0;
	int opt;

	parse_config(av);

	while ((opt = getopt_long(ac, av, "", options, NULL)) != -1) { 
		if (opt == '?') {
			usage(); 
		} else if (modifier(opt) > 0) {
			continue;
		} else if (diskdb_modifier(opt) > 0) { 
			continue;
		} else if (opt == O_ASCII) { 
			argsleft(ac, av);
			no_syslog();
			checkdmi();
			decodefatal(stdin); 
			exit(0);
		} else if (opt == O_VERSION) { 
			noargs(ac, av);
			fprintf(stderr, "mcelog %s\n", MCELOG_VERSION);
			exit(0);
		} else if (diskdb_cmd(opt, ac, av)) {
			exit(0);
		} else if (opt == 0)
			break;		    
	} 
	if (av[optind])
		logfn = av[optind++];
	if (av[optind])
		usage();
	checkdmi();
		
	int fd = open(logfn, O_RDONLY); 
	if (fd < 0) {
		if (ignore_nodev) 
			exit(0);
		Eprintf("Cannot open %s", logfn); 
		exit(1);
	}
	
	if (ioctl(fd, MCE_GET_RECORD_LEN, &recordlen) < 0)
		err("MCE_GET_RECORD_LEN");
	if (ioctl(fd, MCE_GET_LOG_LEN, &loglen) < 0)
		err("MCE_GET_LOG_LEN");

	if (recordlen > sizeof(struct mce))
		Eprintf(
    "warning: record length longer than expected. Consider update.");

	char *buf = calloc(recordlen, loglen); 
	if (!buf) 
		exit(100);

	if (daemon_mode) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
		if (daemon(0, 0) < 0)
			err("daemon");
		for (;;) { 
			int n = poll(&pfd, 1, -1);
			if (n > 0 && (pfd.revents & POLLIN)) 
				process(fd, recordlen, loglen, buf);
		}			
	} else {
		process(fd, recordlen, loglen, buf);
	}
		
	exit(0); 
} 
