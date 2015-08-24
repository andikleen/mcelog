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
#include <linux/limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <ctype.h>
#include <poll.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>
#include <pwd.h>
#include <fnmatch.h>
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
#include "eventloop.h"
#include "memdb.h"
#include "server.h"
#include "trigger.h"
#include "client.h"
#include "msg.h"
#include "yellow.h"
#include "page.h"
#include "bus.h"
#include "unknown.h"

enum cputype cputype = CPU_GENERIC;	

char *logfn = LOG_DEV_FILENAME; 

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
static int foreground;
int filter_memory_errors;
static struct config_cred runcred = { .uid = -1U, .gid = -1U };
static int numerrors;
static char pidfile_default[] = PID_FILE;
static char logfile_default[] = LOG_FILE;
static char *pidfile = pidfile_default;
static char *logfile;
static int debug_numerrors;
int imc_log = -1;
static int check_only = 0;

static int is_cpu_supported(void);


static void disclaimer(void)
{
	Wprintf("Hardware event. This is not a software error.\n");
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

static void resolveaddr(unsigned long long addr)
{
	if (addr && do_dmi && dmi_forced)
		dmi_decodeaddr(addr);
	/* Should check for PCI resources here too */
}

static int mce_filter(struct mce *m, unsigned recordlen)
{
	if (!filter_bogus) 
		return 1;
	/* Filter out known broken MCEs */
	switch (cputype) {
	case CPU_K8:
		return mce_filter_k8(m);
		/* add more buggy CPUs here */
	CASE_INTEL_CPUS:
		return mce_filter_intel(m, recordlen);
	default:
	case CPU_GENERIC:
		return 1;
	}	
}

static void print_tsc(int cpunum, __u64 tsc, unsigned long time) 
{ 
	int ret = -1;
	char *buf = NULL;

	if (cpumhz_forced) 
		ret = decode_tsc_forced(&buf, cpumhz, tsc);
	else if (!time) 
		ret = decode_tsc_current(&buf, cpunum, cputype, cpumhz, tsc);
	Wprintf("TSC %llx %s", tsc, ret >= 0 && buf ? buf : "");
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

static u32 unparse_cpuid(unsigned family, unsigned model)
{
	union { 
		struct cpuid1 c;
		u32 v;	
       } c;

	c.c.family = family;
	if (family >= 0xf) {
		c.c.family = 0xf;
		c.c.ext_family = family - 0xf;
	}
	c.c.model = model & 0xf;
	if (family == 6 || family == 0xf)
		c.c.ext_model = model >> 4;
	return c.v;
}

static char *cputype_name[] = {
	[CPU_GENERIC] = "generic CPU",
	[CPU_P6OLD] = "Intel PPro/P2/P3/old Xeon",
	[CPU_CORE2] = "Intel Core", /* 65nm and 45nm */
	[CPU_K8] = "AMD K8 and derivates",
	[CPU_P4] = "Intel P4",
	[CPU_NEHALEM] = "Intel Xeon 5500 series / Core i3/5/7 (\"Nehalem/Westmere\")",
	[CPU_DUNNINGTON] = "Intel Xeon 7400 series",
	[CPU_TULSA] = "Intel Xeon 7100 series",
	[CPU_INTEL] = "Intel generic architectural MCA",
	[CPU_XEON75XX] = "Intel Xeon 7500 series",
	[CPU_SANDY_BRIDGE] = "Sandy Bridge", /* Fill in better name */
	[CPU_SANDY_BRIDGE_EP] = "Sandy Bridge EP", /* Fill in better name */
	[CPU_IVY_BRIDGE] = "Ivy Bridge", /* Fill in better name */
	[CPU_IVY_BRIDGE_EPEX] = "Intel Xeon v2 (Ivy Bridge) EP/EX", /* Fill in better name */
	[CPU_HASWELL] = "Haswell", /* Fill in better name */
	[CPU_HASWELL_EPEX] = "Intel Xeon v3 (Haswell) EP/EX",
	[CPU_BROADWELL] = "Broadwell",
	[CPU_KNIGHTS_LANDING] = "Knights Landing",
	[CPU_ATOM] = "ATOM",
	[CPU_SKYLAKE] "Skylake",
};

static struct config_choice cpu_choices[] = {
	{ "generic", CPU_GENERIC },
	{ "p6old", CPU_P6OLD },
	{ "core2", CPU_CORE2 },
	{ "k8", CPU_K8 },
	{ "p4", CPU_P4 },
	{ "dunnington", CPU_DUNNINGTON },
	{ "xeon74xx", CPU_DUNNINGTON },
	{ "xeon7400", CPU_DUNNINGTON },
	{ "xeon5500", CPU_NEHALEM },
	{ "xeon5200", CPU_CORE2 },
	{ "xeon5000", CPU_P4 },
	{ "xeon5100", CPU_CORE2 },
	{ "xeon3100", CPU_CORE2 },
	{ "xeon3200", CPU_CORE2 },
	{ "core_i7", CPU_NEHALEM },
	{ "core_i5", CPU_NEHALEM },
	{ "core_i3", CPU_NEHALEM },
	{ "nehalem", CPU_NEHALEM },
	{ "westmere", CPU_NEHALEM },
	{ "xeon71xx", CPU_TULSA },
	{ "xeon7100", CPU_TULSA },
	{ "tulsa", CPU_TULSA },
	{ "intel", CPU_INTEL },
	{ "xeon75xx", CPU_XEON75XX },
	{ "xeon7500", CPU_XEON75XX },
	{ "xeon7200", CPU_CORE2 },
	{ "xeon7100", CPU_P4 },
	{ "sandybridge", CPU_SANDY_BRIDGE }, /* Fill in better name */
	{ "sandybridge-ep", CPU_SANDY_BRIDGE_EP }, /* Fill in better name */
	{ "ivybridge", CPU_IVY_BRIDGE }, /* Fill in better name */
	{ "ivybridge-ep", CPU_IVY_BRIDGE_EPEX },
	{ "ivybridge-ex", CPU_IVY_BRIDGE_EPEX },
	{ "haswell", CPU_HASWELL }, /* Fill in better name */
	{ "haswell-ep", CPU_HASWELL_EPEX },
	{ "haswell-ex", CPU_HASWELL_EPEX },
	{ "broadwell", CPU_BROADWELL },
	{ "knightslanding", CPU_KNIGHTS_LANDING },
	{ "xeon-v2", CPU_IVY_BRIDGE_EPEX },
	{ "xeon-v3", CPU_HASWELL_EPEX },
	{ "atom", CPU_ATOM },
	{ "skylake", CPU_SKYLAKE },
	{ NULL }
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

static unsigned cpuvendor_to_num(char *name)
{
	unsigned i;
	unsigned v;
	char *end;

	v = strtoul(name, &end, 0);
	if (end > name)
		return v;
	for (i = 0; i < NELE(vendor); i++)
		if (!strcmp(name, vendor[i]))
			return i;
	return 0;
}

static char *cpuvendor_name(u32 cpuvendor)
{
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
	static int warned;
	if (m->cpuid) {
		enum cputype t = setup_cpuid(m->cpuvendor, m->cpuid);
		if (!cpu_forced)
			cputype = t;
		else if (t != cputype && t != CPU_GENERIC && !warned) {
			Eprintf("Forced cputype %s does not match cpu type %s from mcelog\n",
				cputype_name[cputype],
				cputype_name[t]);
			warned = 1;
		}
	} else if (cputype == CPU_GENERIC && !cpu_forced) { 
		is_cpu_supported();
	}	
}

static void mce_prepare(struct mce *m)
{
	mce_cpuid(m);
	if (!m->time)
		m->time = time(NULL);
}

static void dump_mce(struct mce *m, unsigned recordlen) 
{
	int n;
	int ismemerr = 0;
	unsigned cpu = m->extcpu ? m->extcpu : m->cpu;

	/* should not happen */
	if (!m->finished)
		Wprintf("not finished?\n");
	Wprintf("CPU %d %s ", cpu, bankname(m->bank));
	if (m->tsc)
		print_tsc(cpu, m->tsc, m->time);
	Wprintf("\n");
	if (m->ip)
		Wprintf("RIP%s %02x:%llx\n", 
		       !(m->mcgstatus & MCG_STATUS_EIPV) ? " !INEXACT!" : "",
		       m->cs, m->ip);
	n = 0;
	if (m->status & MCI_STATUS_MISCV)
		n += Wprintf("MISC %llx ", m->misc);
	if (m->status & MCI_STATUS_ADDRV)
		n += Wprintf("ADDR %llx ", m->addr);		
	if (n > 0)
		Wprintf("\n");
	if (m->time) {
		time_t t = m->time;
		Wprintf("TIME %llu %s", m->time, ctime(&t));
	} 
	switch (cputype) { 
	case CPU_K8:
		decode_k8_mc(m, &ismemerr); 
		break;
	CASE_INTEL_CPUS:
		decode_intel_mc(m, cputype, &ismemerr, recordlen);
		break;
	/* add handlers for other CPUs here */
	default:
		break;
	} 
	/* decode all status bits here */
	Wprintf("STATUS %llx MCGSTATUS %llx\n", m->status, m->mcgstatus);
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
	if (cputype != CPU_SANDY_BRIDGE_EP && cputype != CPU_IVY_BRIDGE_EPEX &&
	    cputype != CPU_HASWELL_EPEX && cputype != CPU_BROADWELL &&
	    cputype != CPU_KNIGHTS_LANDING && cputype != CPU_SKYLAKE)
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
	Wprintf("TSC %#llx\n", m->tsc);
	Wprintf("RIP %#02x:%#llx\n", m->cs, m->ip);
	Wprintf("MISC %#llx\n", m->misc);
	Wprintf("ADDR %#llx\n", m->addr);
	Wprintf("STATUS %#llx\n", m->status);
	Wprintf("MCGSTATUS %#llx\n", m->mcgstatus);
	if (recordlen >= offsetof(struct mce, cpuid))
		Wprintf("PROCESSOR %u:%#x\n", m->cpuvendor, m->cpuid);
#define CPRINT(str, field) 				\
	if (recordlen >= offsetof(struct mce, field))	\
		Wprintf(str "\n", m->field)
	CPRINT("TIME %llu", time);
	CPRINT("SOCKETID %u", socketid);
	CPRINT("APICID %u", apicid);
	CPRINT("MCGCAP %#llx", mcgcap);
#undef CPRINT
	Wprintf("\n");
}

int is_cpu_supported(void)
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
	static int checked;

	if (checked)
		return 1;
	checked = 1;

	f = fopen("/proc/cpuinfo","r");
	if (f != NULL) { 
		int family = 0; 
		int model = 0;
		char vendor[64] = { 0 };
		char *line = NULL;
		size_t linelen = 0; 
		double mhz;

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
			if (!strncmp(line, "flags", 5) && isspace(line[6])) {
				processor_flags = line;
				line = NULL;
				linelen = 0;
				seen |= FLAGS;
			}			      

		} 
		if (seen == ALL) {
			if (!strcmp(vendor,"AuthenticAMD")) {
				if (family == 15) {
					cputype = CPU_K8;
				} else if (family >= 16) {
					SYSERRprintf("ERROR: AMD Processor family %d: mcelog does not support this processor.  Please use the edac_mce_amd module instead.\n", family);
					return 0;
				}
			} else if (!strcmp(vendor,"GenuineIntel"))
				cputype = select_intel_cputype(family, model);
			/* Add checks for other CPUs here */	
		} else {
			Eprintf("warning: Cannot parse /proc/cpuinfo\n"); 
		} 
		fclose(f);
		free(line);
	} else
		Eprintf("warning: Cannot open /proc/cpuinfo\n");

	return 1;
} 

static char *skipspace(char *s)
{
	while (isspace(*s))
		++s;
	return s;
}

static char *skip_syslog(char *s)
{
	char *p;

	/* Handle syslog output */
	p = strstr(s, "mcelog: ");
	if (p)
		return p + sizeof("mcelog: ") - 1;
	return s;
}
	
static char *skipgunk(char *s)
{
	s = skip_syslog(s);

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

	s = skipspace(s);

	if (strncmp(s, "mce: [Hardware Error]:", 22) == 0)
		s += 22;

	return skipspace(s);
}

static inline int urange(unsigned val, unsigned lo, unsigned hi)
{
	return val >= lo && val <= hi;
}

static int is_short(char *name)
{
	return strlen(name) == 3 && 
		isupper(name[0]) && 
		islower(name[1]) &&
		islower(name[2]);
}

static unsigned skip_date(char *s)
{
	unsigned day, hour, min, year, sec; 
	char dayname[11];
	char month[11];
	unsigned next;

	if (sscanf(s, "%10s %10s %u %u:%u:%u %u%n", 
		dayname, month, &day, &hour, &min, &sec, &year, &next) != 7)
		return 0;
	if (!is_short(dayname) || !is_short(month) || !urange(day, 1, 31) ||
		!urange(hour, 0, 24) || !urange(min, 0, 59) || !urange(sec, 0, 59) ||
		year < 1900)
		return 0;
	return next;
}

static void dump_mce_final(struct mce *m, char *symbol, int missing, int recordlen, 
			   int dseen)
{
	m->finished = 1;
	if (m->cpuid)
		mce_cpuid(m);
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
	flushlog();
}

static char *skip_patterns[] = {
	"MCA:*",
	"MCi_MISC register valid*",
	"MC? status*",
	"Unsupported new Family*",
	"Kernel does not support page offline interface",
	NULL
};

static int match_patterns(char *s, char **pat)
{
	for (; *pat; pat++) 
		if (!fnmatch(*pat, s, 0))
			return 0;
	return 1;
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
	char *s = NULL;
	unsigned cpuvendor;
	unsigned recordlen;
	int disclaimer_seen;

	ascii_mode = 1;
	if (do_dmi && dmi_forced)
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
		char *start;

		s = next > 0 ? s + next : line;
		s = skipgunk(s);
		start = s;
		next = 0;

		if (!strncmp(s, "CPU ", 4)) { 
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
				else { 
					m.bank = bank;
					FIELD(bank);
				}
			} else if (n <= 0) { 
				missing++;
			} else if (n > 1) {
				FIELD(mcgstatus);
				m.cpu = cpu;
				if (n > 2) {
					m.bank = bank;
					FIELD(bank);
				} else if (n > 3) 
					FIELD(status);
				if (n < 4)
					missing++; 
			}
		} 
		else if (!strncmp(s, "STATUS", 6)) {
			if ((n = sscanf(s,"STATUS %llx%n", &m.status, &next)) < 1)
				missing++;
			else
				FIELD(status);
		}
		else if (!strncmp(s, "MCGSTATUS", 6)) {
			if ((n = sscanf(s,"MCGSTATUS %llx%n", &m.mcgstatus, &next)) < 1)
				missing++;
			else
				FIELD(mcgstatus);
		}
		else if (!strncmp(s, "RIP", 3)) { 
			unsigned cs = 0; 

			if (!strncmp(s, "RIP !INEXACT!", 13))
				s += 13; 
			else
				s += 3; 

			n = sscanf(s, "%02x:<%016Lx> {%99s}%n",
				   &cs,
				   &m.ip, 
				   symbol, &next); 
			m.cs = cs;
			if (n < 2) 
				missing++; 
			else
				FIELD(ip);
		} 
		else if (!strncmp(s, "TSC",3)) { 
			if ((n = sscanf(s, "TSC %llx%n", &m.tsc, &next)) < 1) 
				missing++;
			else
				FIELD(tsc);
		}
		else if (!strncmp(s, "ADDR",4)) { 
			if ((n = sscanf(s, "ADDR %llx%n", &m.addr, &next)) < 1) 
				missing++;
			else
				FIELD(addr);
		}
		else if (!strncmp(s, "MISC",4)) { 
			if ((n = sscanf(s, "MISC %llx%n", &m.misc, &next)) < 1) 
				missing++; 
			else
				FIELD(misc);
		} 
		else if (!strncmp(s, "PROCESSOR", 9)) { 
			if ((n = sscanf(s, "PROCESSOR %u:%x%n", &cpuvendor, &m.cpuid, &next)) < 2)
				missing++;
			else {
				m.cpuvendor = cpuvendor;			
				FIELD(cpuid);
				FIELD(cpuvendor);
			}
		} 
		else if (!strncmp(s, "TIME", 4)) { 
			if ((n = sscanf(s, "TIME %llu%n", &m.time, &next)) < 1)
				missing++;
			else
				FIELD(time);

			next += skip_date(s + next);
		} 
		else if (!strncmp(s, "MCGCAP", 6)) {
			if ((n = sscanf(s, "MCGCAP %llx%n", &m.mcgcap, &next)) != 1)
				missing++;
			else
				FIELD(mcgcap);
		} 
		else if (!strncmp(s, "APICID", 6)) {
			if ((n = sscanf(s, "APICID %x%n", &m.apicid, &next)) != 1)
				missing++;
			else
				FIELD(apicid);
		} 
		else if (!strncmp(s, "SOCKETID", 8)) {
			if ((n = sscanf(s, "SOCKETID %u%n", &m.socketid, &next)) != 1)
				missing++;
			else
				FIELD(socketid);
		} 
		else if (!strncmp(s, "CPUID", 5)) {
			unsigned fam, mod;
			char vendor[31];

			if ((n = sscanf(s, "CPUID Vendor %30s Family %u Model %u\n", 
					vendor, &fam, &mod)) < 3)
				missing++;
			else {
				m.cpuvendor = cpuvendor_to_num(vendor);
				m.cpuid = unparse_cpuid(fam, mod);
				FIELD(cpuid);
				FIELD(cpuvendor);
			}
		} 
		else if (strstr(s, "HARDWARE ERROR"))
			disclaimer_seen = 1;
		else if (!strncmp(s, "(XEN)", 5)) {
			char *w; 
			unsigned bank, cpu;

			if (strstr(s, "The hardware reports a non fatal, correctable incident occurred")) {
				w = strstr(s, "CPU");
				if (w && sscanf(w, "CPU %d", &cpu)) {
					m.cpu = cpu;
					FIELD(cpu);
				}
			} else if ((n = sscanf(s, "(XEN) Bank %d: %llx at %llx", 
						&bank, &m.status, &m.addr) >= 1)) {
				m.bank = bank;
				FIELD(bank);	
				if (n >= 2) 
					FIELD(status);
				if (n >= 3)
					FIELD(addr);
			}
		}
		else if (!match_patterns(s, skip_patterns))
			n = 0;
		else { 
			s = skipspace(s);
			if (*s && data)
				dump_mce_final(&m, symbol, missing, recordlen, disclaimer_seen); 
			if (!dump_raw_ascii)
				Wprintf("%s", start);
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

static void remove_pidfile(void)
{
	unlink(pidfile);
	if (pidfile != pidfile_default)
		free(pidfile);
}

static void signal_exit(int sig)
{
	remove_pidfile();
	_exit(EXIT_SUCCESS);
}

static void setup_pidfile(char *s)
{
	char cwd[PATH_MAX];
	char *c;

	if (*s != '/') {
		c = getcwd(cwd, PATH_MAX);
		if (!c)
			return;
		asprintf(&pidfile, "%s/%s", cwd, s);
	} else {
		asprintf(&pidfile, "%s", s);
	}

	return;
}

static void write_pidfile(void)
{
	FILE *f;
	atexit(remove_pidfile);
	signal(SIGTERM, signal_exit);
	signal(SIGINT, signal_exit);
	signal(SIGQUIT, signal_exit);
	f = fopen(pidfile, "w");
	if (!f) {
		Eprintf("Cannot open pidfile `%s'", pidfile);
		return;
	}
	fprintf(f, "%u", getpid());
	fclose(f);
}

void usage(void)
{
	fprintf(stderr, 
"Usage:\n"
"  mcelog [options]  [mcelogdevice]\n"
"Decode machine check error records from current kernel.\n"
"  mcelog [options] --daemon\n"
"Run mcelog in daemon mode, waiting for errors from the kernel.\n"
"  mcelog [options] --client\n"
"Query a currently running mcelog daemon for errors\n"
"  mcelog [options] --ascii < log\n"
"  mcelog [options] --ascii --file log\n"
"Decode machine check ASCII output from kernel logs\n"
"Options:\n"  
"--cpu CPU           Set CPU type CPU to decode (see below for valid types)\n"
"--cpumhz MHZ        Set CPU Mhz to decode time (output unreliable, not needed on new kernels)\n"
"--raw		     (with --ascii) Dump in raw ASCII format for machine processing\n"
"--daemon            Run in background waiting for events (needs newer kernel)\n"
"--ignorenodev       Exit silently when the device cannot be opened\n"
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
"--foreground        Keep in foreground (for debugging)\n"
"--num-errors N      Only process N errors (for testing)\n"
"--pidfile file	     Write pid of daemon into file\n"
"--no-imc-log	     Disable extended iMC logging\n"
"--is-cpu-supported  Exit with return code indicating whether the CPU is supported\n"
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
	O_CLIENT,
	O_VERSION,
	O_CONFIG_FILE,
	O_CPU,
	O_FILE,
	O_FOREGROUND,
	O_NUMERRORS,
	O_PIDFILE,
	O_DEBUG_NUMERRORS,
	O_NO_IMC_LOG,
	O_IS_CPU_SUPPORTED,
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
	{ "foreground", 0, NULL, O_FOREGROUND },
	{ "client", 0, NULL, O_CLIENT },
	{ "num-errors", 1, NULL, O_NUMERRORS },
	{ "pidfile", 1, NULL, O_PIDFILE },
	{ "debug-numerrors", 0, NULL, O_DEBUG_NUMERRORS }, /* undocumented: for testing */
	{ "no-imc-log", 0, NULL, O_NO_IMC_LOG },
	{ "is-cpu-supported", 0, NULL, O_IS_CPU_SUPPORTED },
	DISKDB_OPTIONS
	{}
};

static int modifier(int opt)
{
	int v;

	switch (opt) { 
	case O_LOGFILE:
		logfile = optarg;
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
		intel_cpu_init(cputype);
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
			syslog_opt = SYSLOG_REMARK|SYSLOG_ERROR;
		break;
	case O_FILE:
		inputfile = optarg;
		break;
	case O_FOREGROUND:
		foreground = 1;	
		if (!(syslog_opt & SYSLOG_FORCE))
			syslog_opt = SYSLOG_FORCE;
		break;
	case O_NUMERRORS:
		numerrors = atoi(optarg);
		break;
	case O_PIDFILE:
		setup_pidfile(optarg);
		break;
	case O_CONFIG_FILE:
		/* parsed in config.c */
		break;
	case O_DEBUG_NUMERRORS:
		debug_numerrors = 1;
		break;
	case O_NO_IMC_LOG:
		imc_log = 0;
		break;
	case O_IS_CPU_SUPPORTED:
		check_only = 1;
		break;
	case 0:
		break;
	default:
		return 0;
	}
	return 1;
} 

static void modifier_finish(void)
{
	if(!foreground && daemon_mode && !logfile && !(syslog_opt & SYSLOG_LOG)) {
		logfile = logfile_default;
	}
	if (logfile) {
		if (open_logfile(logfile) < 0) {
			if (daemon_mode && !(syslog_opt & SYSLOG_FORCE))
				syslog_opt = SYSLOG_ALL;
			SYSERRprintf("Cannot open logfile %s", logfile);
			if (!daemon_mode)
				exit(1);
		}
	}			
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

static void general_setup(void)
{
	trigger_setup();
	yellow_setup();
	bus_setup();
	unknown_setup();
	config_cred("global", "run-credentials", &runcred);
	if (config_bool("global", "filter-memory-errors") == 1)
		filter_memory_errors = 1;
}

static void drop_cred(void)
{
	if (runcred.uid != -1U && runcred.gid == -1U) {
		struct passwd *pw = getpwuid(runcred.uid);
		if (pw)
			runcred.gid = pw->pw_gid;
	}
	if (runcred.gid != -1U) {
		if (setgid(runcred.gid) < 0) 
			SYSERRprintf("Cannot change group to %d", runcred.gid);
	}
	if (runcred.uid != -1U) {
		if (setuid(runcred.uid) < 0)
			SYSERRprintf("Cannot change user to %d", runcred.uid);
	}
}

static void process(int fd, unsigned recordlen, unsigned loglen, char *buf)
{	
	int i; 
	int len, count;
	int finish = 0, flags;

	if (recordlen == 0) {
		Wprintf("no data in mce record\n");
		return;
	}

	len = read(fd, buf, recordlen * loglen); 
	if (len < 0) {
		SYSERRprintf("mcelog read"); 
		return;
	}

	count = len / (int)recordlen;
	if (count == (int)loglen) {
		if ((ioctl(fd, MCE_GETCLEAR_FLAGS, &flags) == 0) &&
		    (flags & (1 << MCE_OVERFLOW)))
			Eprintf("Warning: MCE buffer is overflowed.\n");
	}

	for (i = 0; (i < count) && !finish; i++) {
		struct mce *mce = (struct mce *)(buf + i*recordlen);
		mce_prepare(mce);
		if (numerrors > 0 && --numerrors == 0)
			finish = 1;
		if (!mce_filter(mce, recordlen)) 
			continue;
		if (!dump_raw_ascii) {
			disclaimer();
			Wprintf("MCE %d\n", i);
			dump_mce(mce, recordlen);
		} else
			dump_mce_raw_ascii(mce, recordlen);
		flushlog();
	}

	if (debug_numerrors && numerrors <= 0)
		finish = 1;

	if (recordlen > sizeof(struct mce))  {
		Eprintf("warning: %lu bytes ignored in each record\n",
				(unsigned long)recordlen - sizeof(struct mce)); 
		Eprintf("consider an update\n"); 
	}

	if (finish)
		exit(0);
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

static void client_command(int ac, char **av)
{
	argsleft(ac, av);
	no_syslog();
	// XXX modifiers
	ask_server("dump all bios\n");		
	ask_server("pages\n");
}

struct mcefd_data {
	unsigned loglen;
	unsigned recordlen;
	char *buf;
};

static void process_mcefd(struct pollfd *pfd, void *data)
{
	struct mcefd_data *d = (struct mcefd_data *)data;
	assert((pfd->revents & POLLIN) != 0);
	process(pfd->fd, d->recordlen, d->loglen, d->buf);
}

static void handle_sigusr1(int sig)
{
	reopenlog();
}

int main(int ac, char **av) 
{ 
	struct mcefd_data d = {};
	int opt;
	int fd;

	parse_config(av);

	while ((opt = getopt_long(ac, av, "", options, NULL)) != -1) { 
		if (opt == '?') {
			usage(); 
		} else if (combined_modifier(opt) > 0) {
			continue;
		} else if (opt == O_ASCII) { 
			ascii_command(ac, av);
			exit(0);
		} else if (opt == O_CLIENT) { 
			client_command(ac, av);
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

	/* before doing anything else let's see if the CPUs are supported */
	if (!cpu_forced && !is_cpu_supported()) {
		if (!check_only)
			fprintf(stderr, "CPU is unsupported\n");
		exit(1);
	}
	if (check_only)
		exit(0);

	/* If the user didn't tell us not to use iMC logging, check if CPU supports it */
	if (imc_log == -1) {
		switch (cputype) {
		case CPU_SANDY_BRIDGE_EP:
		case CPU_IVY_BRIDGE_EPEX:
		case CPU_HASWELL_EPEX:
			imc_log = 1;
			break;
		default:
			imc_log = 0;
			break;
		}
	}

	modifier_finish();
	if (av[optind])
		logfn = av[optind++];
	if (av[optind])
		usage();
	checkdmi();
	general_setup();
		
	fd = open(logfn, O_RDONLY); 
	if (fd < 0) {
		if (ignore_nodev) 
			exit(0);
		SYSERRprintf("Cannot open `%s'", logfn);
		exit(1);
	}
	
	if (ioctl(fd, MCE_GET_RECORD_LEN, &d.recordlen) < 0)
		err("MCE_GET_RECORD_LEN");
	if (ioctl(fd, MCE_GET_LOG_LEN, &d.loglen) < 0)
		err("MCE_GET_LOG_LEN");

	d.buf = xalloc(d.recordlen * d.loglen); 
	if (daemon_mode) {
		prefill_memdb(do_dmi);
		if (!do_dmi)
			closedmi();
		server_setup();
		page_setup();
		if (imc_log)
			set_imc_log(cputype);
		drop_cred();
		register_pollcb(fd, POLLIN, process_mcefd, &d);
		if (!foreground && daemon(0, need_stdout()) < 0)
			err("daemon");
		if (pidfile)
			write_pidfile();
		signal(SIGUSR1, handle_sigusr1);
		event_signal(SIGUSR1);
		eventloop();
	} else {
		process(fd, d.recordlen, d.loglen, d.buf);
	}
	trigger_wait();
		
	exit(0); 
} 
