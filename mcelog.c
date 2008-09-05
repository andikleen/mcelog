/* Copyright (C) 2004,2005,2006 Andi Kleen, SuSE Labs.
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
#include "mcelog.h"
#include "k8.h"
#include "p4.h"
#include "dmi.h"
#include "dimm.h"
#include "tsc.h"
#include "version.h"

enum cputype cputype = CPU_GENERIC;	

char *logfn = "/dev/mcelog";
char *dimm_db_fn = "/var/lib/memory-errors"; 

int use_syslog;
int syslog_level = LOG_WARNING;
int do_dmi;
int ignore_nodev;
int filter_bogus;
int cpu_forced, dmi_forced;
double cpumhz;
char *error_trigger;
unsigned error_thresh = 20;
int ascii_mode;
int dump_raw_ascii;

static void opensyslog(void)
{
	static int syslog_opened;
	if (syslog_opened || ascii_mode)
		return;
	syslog_opened = 1;
	openlog("mcelog", 0, 0);
}

/* For warning messages that should reach syslog */
void Lprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	opensyslog();
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);
}

/* For errors during operation */
void Eprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (isatty(2) && !use_syslog) {
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
	if (use_syslog) {
		vlinesyslog(fmt, ap);
	} else {
		vprintf(fmt, ap);
	}
	va_end(ap);
}

char *bankname(unsigned bank) 
{ 
	static char numeric[64];
	switch (cputype) { 
	case CPU_K8:
		return k8_bank_name(bank);
	case CPU_CORE2:
	case CPU_P4:
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

int mce_filter_k8(struct mce *m)
{	
	/* Filter out GART errors */
	if (m->bank == 4) { 
		unsigned short exterrcode = (m->status >> 16) & 0x0f;
		if (exterrcode == 5 && (m->status & (1ULL<<61)))
			return 0;
	} 
	return 1;
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
	case CPU_CORE2:
	case CPU_P4:
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

void dump_mce(struct mce *m) 
{
	int ismemerr = 0;
	unsigned cpu = m->extcpu ? m->extcpu : m->cpu;

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
	case CPU_CORE2:
	case CPU_P4:
		decode_intel_mc(m, cputype);
		break;
	/* add handlers for other CPUs here */
	default:
		break;
	} 
	/* decode all status bits here */
	Wprintf("STATUS %Lx MCGSTATUS %Lx\n", m->status, m->mcgstatus);
	resolveaddr(m->addr);
	if (ismemerr) { 
		if (open_dimm_db(dimm_db_fn) >= 0) 
			new_error(m->addr, error_thresh, error_trigger);
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
	Wprintf("MCGSTATUS 0x%Lx\n\n", m->mcgstatus);
}

void check_cpu(void)
{ 
	FILE *f;
	f = fopen("/proc/cpuinfo","r");
	if (f != NULL) { 
		int found = 0; 
		int family; 
		char vendor[64];
		char *line = NULL;
		size_t linelen = 0; 
		while (getdelim(&line, &linelen, '\n', f) > 0 && found < 2) { 
			if (sscanf(line, "vendor_id : %63[^\n]", vendor) == 1) 
				found++; 
			if (sscanf(line, "cpu family : %d", &family) == 1)
				found++;
		} 
		if (found == 2) {
			if (!strcmp(vendor,"AuthenticAMD") && 
			    ( family == 15 || family == 16 || family == 17) )
				cputype = CPU_K8;
			if (!strcmp(vendor,"GenuineIntel")) {
				if (family == 15)
					cputype = CPU_P4;
				else if (family == 6)
					cputype = CPU_CORE2;
			}
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

char *skipgunk(char *s)
{
	s = skipspace(s);
	if (*s == '<') { 
		s += strcspn(s, ">"); 
		if (*s == '>') 
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

		s = s ? s + next : line;
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
				n = sscanf(s, "CPU %u %u%n", &cpu, &bank, &next);
				m.cpu = cpu;
				m.bank = bank;
				if (n < 2) 
					missing++;
			} else { 
				m.cpu = cpu;
				m.bank = bank;
				if (n < 4) 
					missing++; 

			}
		} 
		else if (!strncmp(s, "STATUS", 6)) {
			if (sscanf(s,"STATUS %Lx%n", &m.status, &next) < 1)
				missing++;
		}
		else if (!strncmp(s, "MCGSTATUS", 6)) {
			if (sscanf(s,"MCGSTATUS %Lx%n", &m.mcgstatus, &next) < 1)
				missing++;
		}
		else if (!strncmp(s, "RIP", 3)) { 
			unsigned cs = 0; 

			if (!strncmp(s, "RIP!INEXACT!", 12))
				s += 12; 
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
		}
		else { 
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
"Decode machine check error records from kernel\n"
"  mcelog [options] [--dmi] --ascii < log\n"
"Decode machine check ASCII output from kernel logs\n"
"Manage memory error database\n"
"  mcelog [options] --drop-old-memory|--reset-memory locator\n"
"  mcelog --dump-memory locator]\n"
"  old can be either locator or name\n"
"Options:\n"  
"--p4|--k8|--core2|--generic Set CPU type to decode\n"
"--cpumhz MHZ        Set CPU Mhz to decode\n"
"--database fn       Set filename of DIMM database (default %s)\n"
"--error-trigger cmd,thresh   Run cmd on exceeding thresh errors per DIMM\n"
"--raw		     (with --ascii) Dump in raw ASCII format for machine processing\n",
	dimm_db_fn
);
	exit(1);
}

void checkdmi(void)
{
	static int dmi_checked;
	if (dmi_checked)
		return;
	dmi_checked = 1;
	if (dmi_forced && !do_dmi)
		return;
	if (opendmi() < 0) {
		if (dmi_forced)
			exit(1);
		do_dmi = 0;
		return; 
	}
	if (!cpu_forced && !dmi_forced)
		do_dmi = dmi_sanity_check();
}

void checkdimmdb(void)
{
	if (open_dimm_db(dimm_db_fn) < 0) 
		exit(1);
}

char *getarg(char *mod, char *arg, int *gotarg)
{
	if (*mod != '\0') {
		if (mod[0] != '=' || mod[1] == '\0')
			usage();
		return mod + 1;
	}
	if (arg == NULL) 
		usage();
	*gotarg = 2;
	return arg;
}

int modifier(char *s, char *next)
{
	char *arg;
	int gotarg = 1;
	if (!strcmp(s, "--k8")) {
		cputype = CPU_K8;
		cpu_forced = 1;
	} else if (!strcmp(s, "--p4")) {
		cputype = CPU_P4;
		cpu_forced = 1;
	} else if (!strcmp(s, "--generic")) { 
		cputype = CPU_GENERIC;
		cpu_forced = 1;
	} else if (!strcmp(s, "--core2")) { 
		cputype = CPU_CORE2;
		cpu_forced = 1;
	} else if (!strcmp(s, "--ignorenodev")) { 
		ignore_nodev = 1;
	} else if (!strcmp(s,"--filter")) { 
		filter_bogus = 1;			
	} else if (!strcmp(s, "--dmi")) { 
		do_dmi = 1;
		dmi_forced = 1;
	} else if (!strcmp(s, "--no-dmi")) { 
		dmi_forced = 1;
		do_dmi = 0;
	} else if (!strncmp(s, "--dmi-verbose", 13)) { 
		int v;
		arg = getarg(s + 13, next, &gotarg);
		if (sscanf(arg, "%i", &v) != 1)
			usage();
		dmi_set_verbosity(v);
	} else if (!strcmp(s, "--syslog")) { 
		openlog("mcelog", 0, LOG_DAEMON);
		use_syslog = 1;
	} else if (!strncmp(s, "--cpumhz", 9)) { 
		arg = getarg(s + 8, next, &gotarg);
		if (!cpu_forced) {
			fprintf(stderr, 
				"Specify cputype before --cpumhz=..\n");
			usage();
		}
		if (sscanf(arg, "%lf", &cpumhz) != 1)
			usage();
	} else if (!strncmp(s, "--database", 10)) {
		dimm_db_fn = getarg(s + 10, next, &gotarg);
		checkdmi();
		checkdimmdb();
	} else if (!strncmp(s, "--error-trigger", 16)) { 
		char *end;
		arg = getarg(s + 15, next, &gotarg);
		checkdmi();
		open_dimm_db(dimm_db_fn);
		error_thresh = strtoul(arg, &end, 0);
		if (end == arg || *end != ',') 
			usage();
		error_trigger = end + 1; 
	} else if (!strcmp(s, "--syslog-error")) { 
		syslog_level = LOG_ERR;
		use_syslog = 1;
 	} else if (!strcmp(s, "--dump-raw-ascii") || !strcmp(s, "--raw")) {
 		dump_raw_ascii = 1;
	} else
		return 0;
	return gotarg;
} 
	
void argsleft(char **av)
{
	while (*++av) {
		int a = modifier(*av, av[1]);
		if (a == 0) 
			usage();
		av += a - 1;
	}
}

static void dimm_common(char **av)
{
	checkdmi();
	checkdimmdb();
	argsleft(av); 
}

int dimm_cmd(char **av)
{
	if (!strncmp(*av, "--dump-memory", 13)) { 
		char *dimm = NULL; 
		dimm_common(av);
		if ((*av)[13] == '=') 
			dimm = (*av) + 14;
		else if ((*av)[13])
			usage();
		if (dimm) 
			dump_dimm(dimm);
		else
			dump_all_dimms();
		return 1;
	} else if (!strncmp(*av, "--reset-memory", 14)) { 
		char *dimm = NULL;
		dimm_common(av);
		if ((*av)[14] == '=')
			dimm = (*av) + 13;
		else if ((*av)[14])
			usage();
		reset_dimm(dimm);
		return 1;
	} else if (!strcmp(*av, "--drop-old-memory")) { 
		dimm_common(av);
		gc_dimms();
		return 1;
	}
	return 0;	
}

int main(int ac, char **av) 
{ 
	unsigned recordlen = 0;
	unsigned loglen = 0;
	int a;

	check_cpu();

	while (*++av) { 
		if ((a = modifier(*av, av[1])) > 0) {
			av += a - 1;
		} else if (!strcmp(*av, "--ascii")) {
			argsleft(av);
			checkdmi();
			decodefatal(stdin); 
			exit(0);
		} else if (!strcmp(*av, "--version")) { 
			fprintf(stderr, "mcelog %s\n", MCELOG_VERSION);
			exit(0);
		} else if (dimm_cmd(av)) {
			exit(0);
		} else if (!strncmp(*av, "--", 2)) { 
			if (av[0][2] == '\0') 
				break;
			usage();
		}
	} 
	if (*av)
		logfn = *av++;
	if (*av)
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

	exit(0); 
} 
