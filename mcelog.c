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
#include <errno.h>
#include <stddef.h>
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
#include "memutil.h"

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
int filter_bogus = 1;
int cpu_forced;
static double cpumhz;
static int cpumhz_forced;
int ascii_mode;
int dump_raw_ascii;
int daemon_mode;
static char *inputfile;
char *processor_flags;

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
static int vlinesyslog(char *fmt, va_list ap)
{
	static char line[200];
	int n;
	int lend = strlen(line); 
	int w = vsnprintf(line + lend, sizeof(line)-lend, fmt, ap);
	while (line[n = strcspn(line, "\n")] != 0) {
		line[n] = 0;
		syslog(syslog_level, "%s", line);
		memmove(line, line + n + 1, strlen(line + n + 1) + 1);
	}
	return w;
}

/* For decoded machine check output */
int Wprintf(char *fmt, ...)
{
	int n;
	va_list ap;
	va_start(ap,fmt);
	if (syslog_opt & SYSLOG_ERROR) {
		opensyslog();
		n = vlinesyslog(fmt, ap);
	} else {
		n = vprintf(fmt, ap);
	}
	va_end(ap);
	return n;
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

static void disclaimer(void)
{
	Wprintf("HARDWARE ERROR. This is *NOT* a software problem!\n");
	Wprintf("Please contact your hardware vendor\n");
}

static char *extended_bankname(unsigned bank) 
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

static char *bankname(unsigned bank) 
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

static void resolveaddr(unsigned long addr)
{
	if (addr && do_dmi)
		dmi_decodeaddr(addr);
	/* Should check for PCI resources here too */
}

static int mce_filter(struct mce *m)
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

static void print_tsc(int cpunum, __u64 tsc, unsigned long time) 
{ 
	int ret;
	char *buf = NULL;

	if (cpumhz_forced) 
		ret = decode_tsc_forced(&buf, cpumhz, tsc);
	else if (!time) 
		ret = decode_tsc_current(&buf, cpunum, cputype, cpumhz, tsc);
	Wprintf("TSC %Lx %s", tsc, ret >= 0 && buf ? buf : "");
	free(buf);
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

static struct config_choice cpu_choices[] = {
	{ "generic", CPU_GENERIC },
	{ "p6old", CPU_P6OLD },
	{ "core2", CPU_CORE2 },
	{ "k8", CPU_K8 },
	{ "p4", CPU_P4 },
	{ "dunnington", CPU_DUNNINGTON },
	{ "xeon74xx", CPU_DUNNINGTON },
	{ "xeon75xx", CPU_NEHALEM },
	{ "core_i7", CPU_NEHALEM },
	{ "nehalem", CPU_NEHALEM },
	{ "xeon71xx", CPU_TULSA },
	{ "tulsa", CPU_TULSA },
	{}
};

static void print_cputypes(void)
{
	struct config_choice *c;
	fprintf(stderr, "Valid CPUs:");
	for (c = cpu_choices; c->name; c++)
		fprintf(stderr, " %s", c->name);
	fputc('\n', stderr);
}

static enum cputype lookup_cputype(char *name)
{
	struct config_choice *c;
	for (c = cpu_choices; c->name; c++) {
		if (!strcasecmp(name, c->name))
			return c->val;
	}
	fprintf(stderr, "Unknown CPU type `%s' specified\n", name);
	print_cputypes();
	exit(1);
}

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
			Eprintf("Forced cputype %s does not match cpu type %s from mcelog\n",
				cputype_name[cputype],
				cputype_name[t]);
	} else if (cputype == CPU_GENERIC && !cpu_forced) { 
		check_cpu();
	}	
}

static void dump_mce(struct mce *m, unsigned recordlen) 
{
	int n;
	int ismemerr = 0;
	unsigned cpu = m->extcpu ? m->extcpu : m->cpu;

	mce_cpuid(m);
	/* should not happen */
	if (!m->finished)
		Wprintf("not finished?\n");
	Wprintf("CPU %d %s ", cpu, bankname(m->bank));
	if (m->tsc)
		print_tsc(cpu, m->tsc, m->time);
	Wprintf("\n");
	if (m->ip)
		Wprintf("RIP%s %02x:%Lx\n", 
		       !(m->mcgstatus & MCG_STATUS_EIPV) ? " !INEXACT!" : "",
		       m->cs, m->ip);
	n = 0;
	if (m->status & MCI_STATUS_MISCV)
		n += Wprintf("MISC %Lx ", m->misc);
	if (m->status & MCI_STATUS_ADDRV)
		n += Wprintf("ADDR %Lx ", m->addr);		
	if (n > 0)
		Wprintf("\n");
	if (m->time) {
		time_t t = m->time;
		n += Wprintf("TIME %Lu %s", m->time, ctime(&t));
	}
	switch (cputype) { 
	case CPU_K8:
		decode_k8_mc(m, &ismemerr); 
		break;
	CASE_INTEL_CPUS:
		decode_intel_mc(m, cputype, &ismemerr);
		break;
	/* add handlers for other CPUs here */
	default:
		break;
	} 
	/* decode all status bits here */
	Wprintf("STATUS %Lx MCGSTATUS %Lx\n", m->status, m->mcgstatus);
	n = 0;
	if (recordlen >= offsetof(struct mce, cpuid) && m->mcgcap)
		n += Wprintf("MCGCAP %llx ", m->mcgcap);
	if (recordlen >= offsetof(struct mce, apicid))
		n += Wprintf("APICID %x ", m->apicid);
	if (recordlen >= offsetof(struct mce, socketid))
		n += Wprintf("SOCKETID %x ", m->socketid);
	if (n > 0)
		Wprintf("\n");

	if (recordlen >= offsetof(struct mce, cpuid) && m->cpuid) {
		u32 fam, mod;
		parse_cpuid(m->cpuid, &fam, &mod);
		Wprintf("CPUID Vendor %s Family %u Model %u\n",
			cpuvendor_name(m->cpuvendor), 
			fam,
			mod);
	}
	resolveaddr(m->addr);
	if (!ascii_mode && ismemerr && (m->status & MCI_STATUS_ADDRV)) {
		diskdb_resolve_addr(m->addr);
	}
}

static void dump_mce_raw_ascii(struct mce *m, unsigned recordlen)
{
	/* should not happen */
	if (!m->finished)
		Wprintf("not finished?\n");
	Wprintf("CPU %u\n", m->extcpu ? m->extcpu : m->cpu);
	Wprintf("BANK %d\n", m->bank);
	Wprintf("TSC 0x%Lx\n", m->tsc);
	Wprintf("RIP 0x%02x:0x%Lx\n", m->cs, m->ip);
	Wprintf("MISC 0x%Lx\n", m->misc);
	Wprintf("ADDR 0x%Lx\n", m->addr);
	Wprintf("STATUS 0x%Lx\n", m->status);
	Wprintf("MCGSTATUS 0x%Lx\n", m->mcgstatus);
	if (recordlen >= offsetof(struct mce, cpuid))
		Wprintf("PROCESSOR %u:0x%x\n", m->cpuvendor, m->cpuid);
#define CPRINT(str, field) 				\
	if (recordlen >= offsetof(struct mce, field))	\
		Wprintf(str "\n", m->field)
	CPRINT("TIME %llu", time);
	CPRINT("SOCKETID %u", socketid);
	CPRINT("APICID %u", apicid);
	CPRINT("MCGCAP %llx", mcgcap);
#undef CPRINT
	Wprintf("\n");
}

void check_cpu(void)
{ 
	enum { 
		VENDOR = 1, 
		FAMILY = 2, 
		MODEL = 4, 
		MHZ = 8, 
		FLAGS = 16, 
		ALL = 0x1f 
	} seen = 0;
	FILE *f;
	f = fopen("/proc/cpuinfo","r");
	if (f != NULL) { 
		int family; 
		int model;
		char vendor[64];
		char *line = NULL;
		size_t linelen = 0; 
		double mhz;
		int n;
		while (getdelim(&line, &linelen, '\n', f) > 0 && seen != ALL) { 
			if (sscanf(line, "vendor_id : %63[^\n]", vendor) == 1) 
				seen |= VENDOR;
			if (sscanf(line, "cpu family : %d", &family) == 1)
				seen |= FAMILY;
			if (sscanf(line, "model : %d", &model) == 1)
				seen |= MODEL;
			/* We use only Mhz of the first CPU, assuming they are the same
			   (there are more sanity checks later to make this not as wrong
			   as it sounds) */
			if (sscanf(line, "cpu MHz : %lf", &mhz) == 1) { 
				if (!cpumhz_forced)
					cpumhz = mhz;
				seen |= MHZ;
			}
			n = 0;
			if (sscanf(line, "flags : %n", &n) == 0 && n > 0) {
				processor_flags = line + 7;
				line = NULL;
				linelen = 0;
				seen |= FLAGS;
			}			      

		} 
		if (seen == ALL) {
			if (cpu_forced) 
				;
			else if (!strcmp(vendor,"AuthenticAMD") && 
			    (family == 15 || family == 16 || family == 17))
				cputype = CPU_K8;
			else if (!strcmp(vendor,"GenuineIntel"))
				cputype = select_intel_cputype(family, model);
			/* Add checks for other CPUs here */	
		} else {
			Eprintf("warning: Cannot parse /proc/cpuinfo\n"); 
		} 
		fclose(f);
		free(line);
	} else
		Eprintf("warning: Cannot open /proc/cpuinfo\n");
} 

static char *skipspace(char *s)
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

static void dump_mce_final(struct mce *m, char *symbol, int missing, int recordlen, 
			   int dseen)
{
	m->finished = 1;
	if (!dump_raw_ascii) {
		if (!dseen)
			disclaimer();
		dump_mce(m, recordlen);
		if (symbol[0])
			Wprintf("RIP: %s\n", symbol);
		if (missing) 
			Wprintf("(Fields were incomplete)\n");
	} else
		dump_mce_raw_ascii(m, recordlen);
}

#define FIELD(f) \
	if (recordlen < endof_field(struct mce, f)) \
		recordlen = endof_field(struct mce, f)

/* Decode ASCII input for fatal messages */
static void decodefatal(FILE *inf)
{
	struct mce m;
	char *line = NULL; 
	size_t linelen = 0;
	int missing; 
	char symbol[100];
	int data;
	int next;
	char *s;
	unsigned cpuvendor;
	unsigned recordlen;
	int disclaimer_seen;

	ascii_mode = 1;
	if (do_dmi)
		Wprintf(
 "WARNING: with --dmi mcelog --ascii must run on the same machine with the\n"
 "     same BIOS/memory configuration as where the machine check occurred.\n");

restart:
	missing = 0;
	data = 0;
	next = 0;
	disclaimer_seen = 0;
	recordlen = 0;
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
				   &m.ip, 
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
		} 
		else if (!strncmp(s, "PROCESSOR", 9)) { 
			if ((n = sscanf(s, "PROCESSOR %u:%x%n", &cpuvendor, &m.cpuid, &next)) < 2)
				missing++;
			else
				m.cpuvendor = cpuvendor;			
			FIELD(cpuvendor);
		} 
		else if (!strncmp(s, "TIME", 4)) { 
			if ((n = sscanf(s, "TIME %llu%n", &m.time, &next)) < 1)
				missing++;
			FIELD(time);
		} 
		else if (!strncmp(s, "MCGCAP", 6)) {
			if ((n = sscanf(s, "MCGCAP %llx%n", &m.mcgcap, &next)) != 1)
				missing++;
			FIELD(mcgcap);
		} 
		else if (!strncmp(s, "APICID", 6)) {
			if ((n = sscanf(s, "APICID %x%n", &m.apicid, &next)) != 1)
				missing++;
			FIELD(apicid);
		} 
		else if (!strncmp(s, "SOCKETID", 8)) {
			if ((n = sscanf(s, "SOCKETID %u%n", &m.socketid, &next)) != 1)
				missing++;
			FIELD(socketid);
		} 
		else if (strstr(s, "HARDWARE ERROR"))
			disclaimer_seen = 1;
		else { 
			s = skipspace(s);
			if (*s && data)
				dump_mce_final(&m, symbol, missing, recordlen, disclaimer_seen); 
			if (!dump_raw_ascii)
				Wprintf("%s", line);
			if (*s && data)
				goto restart;
		} 
		if (n > 0) 
			data = 1;
	} 
	free(line);
	if (data)
		dump_mce_final(&m, symbol, missing, recordlen, disclaimer_seen);
}

void usage(void)
{
	fprintf(stderr, 
"Usage:\n"
"  mcelog [options]  [mcelogdevice]\n"
"Decode machine check error records from kernel.\n"
"Normally this is invoked from a cronjob or using the kernel trigger.\n"
"  mcelog [options] --ascii < log\n"
"  mcelog [options] --ascii --file log\n"
"Decode machine check ASCII output from kernel logs\n"
"Options:\n"  
"--cpu CPU           Set CPU type CPU to decode (see below for valid types)\n"
"--cpumhz MHZ        Set CPU Mhz to decode time (output unreliable, not needed on new kernels)\n"
"--raw		     (with --ascii) Dump in raw ASCII format for machine processing\n"
"--daemon            Run in background polling for events (needs newer kernel)\n"
"--file filename     With --ascii read machine check log from filename instead of stdin\n"
"--syslog            Log decoded machine checks in syslog (default stdout or syslog for daemon)\n"	     
"--syslog-error	     Log decoded machine checks in syslog with error level\n"
"--no-syslog         Never log anything to syslog\n"
"--logfile filename  Append log output to logfile instead of stdout\n"
"--dmi               Use SMBIOS information to decode DIMMs (needs root)\n"
"--no-dmi            Don't use SMBIOS information\n"
"--dmi-verbose       Dump SMBIOS information (for debugging)\n"
"--filter            Inhibit known bogus events (default on)\n"
"--no-filter         Don't inhibit known broken events\n"
"--config-file filename Read config information from config file instead of " CONFIG_FILENAME "\n"
		);
	diskdb_usage();
	print_cputypes();
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
	O_CPU,
	O_FILE,
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
	{ "no-filter", 0, &filter_bogus, 0 },
	{ "dmi", 0, NULL, O_DMI },
	{ "no-dmi", 0, NULL, O_NO_DMI },
	{ "dmi-verbose", 1, NULL, O_DMI_VERBOSE },
	{ "syslog", 0, NULL, O_SYSLOG },
	{ "cpumhz", 1, NULL, O_CPUMHZ },
	{ "syslog-error", 0, NULL, O_SYSLOG_ERROR },
	{ "dump-raw-ascii", 0, &dump_raw_ascii, 1 },
	{ "raw", 0, &dump_raw_ascii, 1 },
	{ "no-syslog", 0, NULL, O_NO_SYSLOG },
	{ "daemon", 0, NULL, O_DAEMON },
	{ "ascii", 0, NULL, O_ASCII },
	{ "file", 1, NULL, O_FILE },
	{ "version", 0, NULL, O_VERSION },
	{ "config-file", 1, NULL, O_CONFIG_FILE },
	{ "cpu", 1, NULL, O_CPU },
	DISKDB_OPTIONS
	{}
};

static int modifier(int opt)
{
	int v;

	switch (opt) { 
	case O_LOGFILE:
		fclose(stdout);
		if (!freopen(optarg, "a", stdout)) {
			fprintf(stderr, "Cannot open log file %s. Exiting.\n", optarg);	
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
	case O_CPU:
		cputype = lookup_cputype(optarg);
		cpu_forced = 1;
		break;
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
		cpumhz_forced = 1;
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
	case O_FILE:
		inputfile = optarg;
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
	int i; 
	int len;

	if (recordlen == 0) {
		Wprintf("no data in mce record\n");
		return;
	}

	len = read(fd, buf, recordlen * loglen); 
	if (len < 0) 
		err("read"); 

	for (i = 0; i < len / (int)recordlen; i++) { 
		struct mce *mce = (struct mce *)(buf + i*recordlen);
		if (!mce_filter(mce)) 
			continue;
		if (!dump_raw_ascii) {
			disclaimer();
			Wprintf("MCE %d\n", i);
			dump_mce(mce, recordlen);
		} else
			dump_mce_raw_ascii(mce, recordlen);
	}

	if (recordlen > sizeof(struct mce))  {
		Eprintf("warning: %lu bytes ignored in each record\n",
				(unsigned long)recordlen - sizeof(struct mce)); 
		Eprintf("consider an update\n"); 
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
			fprintf(stderr, "Cannot open config file %s\n", fn);
			exit(1);
		}
		return;
	}
	config_options(options, combined_modifier);
}

static void ascii_command(int ac, char **av)
{
	FILE *f = stdin;

	argsleft(ac, av);
	if (inputfile) { 
		f = fopen(inputfile, "r");
		if (!f) {		
			fprintf(stderr, "Cannot open input file `%s': %s\n",
				inputfile, strerror(errno));
			exit(1);
		}
		/* f closed by exit */
	}
	no_syslog();
	checkdmi();
	decodefatal(f); 
}

int main(int ac, char **av) 
{ 
	unsigned recordlen = 0;
	unsigned loglen = 0;
	int opt;
	int fd;
	char *buf;

	parse_config(av);

	while ((opt = getopt_long(ac, av, "", options, NULL)) != -1) { 
		if (opt == '?') {
			usage(); 
		} else if (combined_modifier(opt) > 0) {
			continue;
		} else if (opt == O_ASCII) { 
			ascii_command(ac, av);
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
		
	fd = open(logfn, O_RDONLY); 
	if (fd < 0) {
		if (ignore_nodev) 
			exit(0);
		fprintf(stderr, "Cannot open %s", logfn); 
		exit(1);
	}
	
	if (ioctl(fd, MCE_GET_RECORD_LEN, &recordlen) < 0)
		err("MCE_GET_RECORD_LEN");
	if (ioctl(fd, MCE_GET_LOG_LEN, &loglen) < 0)
		err("MCE_GET_LOG_LEN");

	buf = xalloc(recordlen * loglen); 
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
