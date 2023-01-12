/* Copyright (c) 2005 by Intel Corp.

   Decode Intel machine check (generic and P4 specific)

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
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
   
   Authors:
        Racing Guo <racing.guo@intel.com>
	Andi Kleen
*/
#include <stdio.h>
#include <stddef.h>
#include "mcelog.h"
#include "p4.h"
#include "core2.h"
#include "nehalem.h"
#include "dunnington.h"
#include "tulsa.h"
#include "intel.h"
#include "yellow.h"
#include "bus.h"
#include "unknown.h"
#include "bitfield.h"
#include "sandy-bridge.h"
#include "ivy-bridge.h"
#include "haswell.h"
#include "broadwell_de.h"
#include "broadwell_epex.h"
#include "skylake_xeon.h"
#include "denverton.h"
#include "i10nm.h"
#include "sapphire.h"

/* decode mce for P4/Xeon and Core2 family */

static char* get_TT_str(__u8 t)
{
	static char* TT[] = {"Instruction", "Data", "Generic", "Unknown"};
	if (t >= NELE(TT)) {
		return "UNKNOWN";
	}

	return TT[t];
}

static char* get_LL_str(__u8 ll)
{
	static char* LL[] = {"Level-0", "Level-1", "Level-2", "Level-3"};
	if (ll >= NELE(LL)) {
		return "UNKNOWN";
	}

	return LL[ll];
}

static char* get_RRRR_str(__u8 rrrr)
{
	static struct {
		__u8 value;
		char* str;
	} RRRR [] = {
		{0, "Generic"}, {1, "Read"},
		{2, "Write" }, {3, "Data-Read"},
		{4, "Data-Write"}, {5, "Instruction-Fetch"},
		{6, "Prefetch"}, {7, "Eviction"},
		{8, "Snoop"}
	};
	unsigned i;

	for (i = 0; i < (int)NELE(RRRR); i++) {
		if (RRRR[i].value == rrrr) {
			return RRRR[i].str;
		}
	}

	return "UNKNOWN";
}

static char* get_PP_str(__u8 pp)
{
	static char* PP[] = {
		"Local-CPU-originated-request",
		"Responed-to-request",
		"Observed-error-as-third-party",
		"Generic"
	};
	if (pp >= NELE(PP)) {
		return "UNKNOWN";
	}

	return PP[pp];
}

static char* get_T_str(__u8 t)
{
	static char* T[] = {"Request-did-not-timeout", "Request-timed-out"};
	if (t >= NELE(T)) {
		return "UNKNOWN";
	}

	return T[t];
}

static char* get_II_str(__u8 i)
{
	static char* II[] = {"Memory-access", "Reserved", "IO", "Other-transaction"};

	if (i >= NELE(II)) {
		return "UNKNOWN";
	}

	return II[i];
}

static int decode_mca(u64 status, u64 misc, u64 track, int cpu, int *ismemerr, int socket,
			u8 bank)
{
#define TLB_LL_MASK      0x3  /*bit 0, bit 1*/
#define TLB_LL_SHIFT     0x0
#define TLB_TT_MASK      0xc  /*bit 2, bit 3*/
#define TLB_TT_SHIFT     0x2 

#define CACHE_LL_MASK    0x3  /*bit 0, bit 1*/
#define CACHE_LL_SHIFT   0x0
#define CACHE_TT_MASK    0xc  /*bit 2, bit 3*/
#define CACHE_TT_SHIFT   0x2
#define CACHE_RRRR_MASK  0xF0 /*bit 4, bit 5, bit 6, bit 7 */
#define CACHE_RRRR_SHIFT 0x4

#define BUS_LL_MASK      0x3  /* bit 0, bit 1*/
#define BUS_LL_SHIFT     0x0
#define BUS_II_MASK      0xc  /*bit 2, bit 3*/
#define BUS_II_SHIFT     0x2
#define BUS_RRRR_MASK    0xF0 /*bit 4, bit 5, bit 6, bit 7 */
#define BUS_RRRR_SHIFT   0x4
#define BUS_T_MASK       0x100 /*bit 8*/
#define BUS_T_SHIFT      0x8   
#define BUS_PP_MASK      0x600 /*bit 9, bit 10*/
#define BUS_PP_SHIFT     0x9

	u32 mca;
	int ret = 0;
	static char *msg[] = {
		[0] = "No Error",
		[1] = "Unclassified",
		[2] = "Microcode ROM parity error",
		[3] = "External error",
		[4] = "FRC error",
		[5] = "Internal parity error",
		[6] = "SMM Handler Code Access Violation",
	};

	mca = status & 0xffff;
	if (mca & (1UL << 12)) {
		Wprintf("corrected filtering (some unreported errors in same region)\n");
		mca &= ~(1UL << 12);
	}

	if (mca < NELE(msg)) {
		Wprintf("%s\n", msg[mca]); 
		return ret;
	}

	if ((mca >> 2) == 3) { 
		unsigned levelnum;
		char *level;
		levelnum = mca & 3;
		level = get_LL_str(levelnum);
		Wprintf("%s Generic cache hierarchy error\n", level);
		if (track == 2)
			run_yellow_trigger(cpu, -1, levelnum, "unknown", level, socket);
	} else if (test_prefix(4, mca)) {
		unsigned levelnum, typenum;
		char *level, *type;
		typenum = (mca & TLB_TT_MASK) >> TLB_TT_SHIFT;
		type = get_TT_str(typenum);
		levelnum = (mca & TLB_LL_MASK) >> TLB_LL_SHIFT;
		level = get_LL_str(levelnum);
		Wprintf("%s TLB %s Error\n", type, level);
		if (track == 2)
			run_yellow_trigger(cpu, typenum, levelnum, type, level, socket);
	} else if (test_prefix(8, mca)) {
		unsigned typenum = (mca & CACHE_TT_MASK) >> CACHE_TT_SHIFT;
		unsigned levelnum = ((mca & CACHE_LL_MASK) >> CACHE_LL_SHIFT) + 1;
		char *type = get_TT_str(typenum);
		char *level = get_LL_str(levelnum);
		Wprintf("%s CACHE %s %s Error\n", type, level,
				get_RRRR_str((mca & CACHE_RRRR_MASK) >> 
					      CACHE_RRRR_SHIFT));
		if (track == 2)
			run_yellow_trigger(cpu, typenum, levelnum, type, level,socket);
	} else if (test_prefix(9, mca) && EXTRACT(mca, 7, 8) == 1) {
		Wprintf("Memory as cache: ");
		decode_memory_controller(mca, bank);
	} else if (test_prefix(10, mca)) {
		if (mca == 0x400)
			Wprintf("Internal Timer error\n");
		else
			Wprintf("Internal unclassified error: %x\n", mca & 0xffff);

		ret = 1;
	} else if (test_prefix(11, mca)) {
		char *level, *pp, *rrrr, *ii, *timeout;

		level = get_LL_str((mca & BUS_LL_MASK) >> BUS_LL_SHIFT);
		pp = get_PP_str((mca & BUS_PP_MASK) >> BUS_PP_SHIFT);
		rrrr = get_RRRR_str((mca & BUS_RRRR_MASK) >> BUS_RRRR_SHIFT);
		ii = get_II_str((mca & BUS_II_MASK) >> BUS_II_SHIFT);
		timeout = get_T_str((mca & BUS_T_MASK) >> BUS_T_SHIFT);

		Wprintf("BUS error: %d %d %s %s %s %s %s\n", socket, cpu,
			level, pp, rrrr, ii, timeout);
		run_bus_trigger(socket, cpu, level, pp, rrrr, ii, timeout);
		/* IO MCA - reported as bus/interconnect with specific PP,T,RRRR,II,LL values
		 * and MISCV set. MISC register points to root port that reported the error
		 * need to cross check with AER logs for more details.
		 * See: http://www.intel.com/content/www/us/en/architecture-and-technology/enhanced-mca-logging-xeon-paper.html
		 */
		if ((status & MCI_STATUS_MISCV) &&
		    (status & 0xefff) == 0x0e0b) {
			int	seg, bus, dev, fn;

			seg = EXTRACT(misc, 32, 39);
			bus = EXTRACT(misc, 24, 31);
			dev = EXTRACT(misc, 19, 23);
			fn = EXTRACT(misc, 16, 18);
			Wprintf("IO MCA reported by root port %x:%02x:%02x.%x\n",
				seg, bus, dev, fn);
			run_iomca_trigger(socket, cpu, seg, bus, dev, fn);
		}
	} else if (test_prefix(7, mca)) {
		decode_memory_controller(mca, bank);
		*ismemerr = 1;
	} else {
		Wprintf("Unknown Error %x\n", mca);
		ret = 1;
	}
	return ret;
}

static void p4_decode_model(__u32 model)
{
	static struct {
		int value;
		char *str;
	}MD []= {
		{16, "FSB address parity"},
		{17, "Response hard fail"},
		{18, "Response parity"},
		{19, "PIC and FSB data parity"},
		{20, "Invalid PIC request(Signature=0xF04H)"},
		{21, "Pad state machine"},
		{22, "Pad strobe glitch"},
		{23, "Pad address glitch"}
	};
	unsigned i;

	Wprintf("Model:");
	for (i = 0; i < NELE(MD); i++) {
		if (model & (1 << MD[i].value))
			Wprintf("%s\n",MD[i].str);
	}
	Wprintf("\n");
}

static void decode_tracking(u64 track)
{
	static char *msg[] = { 
		[1] = "green", 
		[2] = "yellow\n"
"Large number of corrected cache errors. System operating, but might lead\n"
"to uncorrected errors soon",
		[3] ="res3" };
	if (track) {
		Wprintf("Threshold based error status: %s\n", msg[track]);
	}
}

static const char *arstate[4] = { 
	[0] = "UCNA",
	[1] = "AR", 	
	[2] = "SRAO",
	[3] = "SRAR"
};

static const char *ce_types[] = {
	[0] = "ecc",
	[1] = "mirroring with channel failover",
	[2] = "mirroring. Primary channel scrubbed successfully"
};

static int check_for_mirror(__u8 bank, __u64  status, __u64 misc)
{
	switch (cputype) {
	case CPU_BROADWELL_EPEX:
		return bdw_epex_ce_type(bank, status, misc);
	case CPU_SKYLAKE_XEON:
		return skylake_s_ce_type(bank, status, misc);
	case CPU_ICELAKE_XEON:
		return i10nm_ce_type(bank, status, misc);
	case CPU_SAPPHIRERAPIDS:
	case CPU_EMERALDRAPIDS:
	default:
		return 0;
	}
}

static int decode_mci(__u64 status, __u64 misc, int cpu, unsigned mcgcap, int *ismemerr,
		       int socket, __u8 bank)
{
	u64 track = 0;
	int i;

	Wprintf("MCi status:\n");
	if (!(status & MCI_STATUS_VAL))
		Wprintf("Machine check not valid\n");

	if (status & MCI_STATUS_OVER)
		Wprintf("Error overflow\n");
	
	if (status & MCI_STATUS_UC) 
		Wprintf("Uncorrected error\n");
	else if ((i = check_for_mirror(bank, status, misc)))
		Wprintf("Corrected error by %s\n", ce_types[i]);
	else
		Wprintf("Corrected error\n");

	if (status & MCI_STATUS_EN)
		Wprintf("Error enabled\n");

	if (status & MCI_STATUS_MISCV) 
		Wprintf("MCi_MISC register valid\n");

	if (status & MCI_STATUS_ADDRV)
		Wprintf("MCi_ADDR register valid\n");

	if (status & MCI_STATUS_PCC)
		Wprintf("Processor context corrupt\n");

	if (status & (MCI_STATUS_S|MCI_STATUS_AR))
		Wprintf("%s\n", arstate[(status >> 55) & 3]);

	if ((mcgcap & MCG_SER_P) && (status & MCI_STATUS_FWST)) {
		Wprintf("Firmware may have updated this error\n");
	}

	if ((mcgcap == 0 || (mcgcap & MCG_TES_P)) && !(status & MCI_STATUS_UC)) {
		track = (status >> 53) & 3;
		decode_tracking(track);
	}
	Wprintf("MCA: ");
	return decode_mca(status, misc, track, cpu, ismemerr, socket, bank);
}

static void decode_mcg(__u64 mcgstatus)
{
	Wprintf("MCG status:");
	if (mcgstatus & MCG_STATUS_RIPV)
		Wprintf("RIPV ");
	if (mcgstatus & MCG_STATUS_EIPV)
		Wprintf("EIPV ");
	if (mcgstatus & MCG_STATUS_MCIP)
		Wprintf("MCIP ");
	if (mcgstatus & MCG_STATUS_LMCES)
		Wprintf("LMCE ");
	Wprintf("\n");
}

static void decode_thermal(struct mce *log, int cpu)
{
	if (log->status & 1) {
		Gprintf(
"Processor %d heated above trip temperature. Throttling enabled.\n", cpu);
		Gprintf(
"Please check your system cooling. Performance will be impacted\n");
	} else { 
		Gprintf("Processor %d below trip temperature. Throttling disabled\n", cpu);
	} 
}

void decode_intel_mc(struct mce *log, int cputype, int *ismemerr, unsigned size)
{
	int socket = size > offsetof(struct mce, socketid) ? (int)log->socketid : -1;
	int cpu = log->extcpu ? log->extcpu : log->cpu;

	if (log->bank == MCE_THERMAL_BANK) { 
		decode_thermal(log, cpu);
		run_unknown_trigger(socket, cpu, log);
		return;
	}

	decode_mcg(log->mcgstatus);
	if (decode_mci(log->status, log->misc, cpu, log->mcgcap, ismemerr,
		socket, log->bank))
		run_unknown_trigger(socket, cpu, log);

	if (test_prefix(11, (log->status & 0xffffL))) {
		switch (cputype) {
		case CPU_P6OLD:
			p6old_decode_model(log->status);
			break;
		case CPU_DUNNINGTON:
		case CPU_CORE2:
			core2_decode_model(log->status);
			break;
		case CPU_TULSA:
		case CPU_P4:
			p4_decode_model(log->status & 0xffff0000L);
			break;
		case CPU_NEHALEM:
		case CPU_XEON75XX:
			core2_decode_model(log->status);
			break;
		}
	}

	/* Model specific addon information */
	switch (cputype) { 
	case CPU_NEHALEM:
		nehalem_decode_model(log->status, log->misc);
		break;
	case CPU_DUNNINGTON:
		dunnington_decode_model(log->status);
		break;
	case CPU_TULSA:
		tulsa_decode_model(log->status, log->misc);
		break;
	case CPU_XEON75XX:
		xeon75xx_decode_model(log, size);
		break;
	case CPU_SANDY_BRIDGE:
	case CPU_SANDY_BRIDGE_EP:
		snb_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	case CPU_IVY_BRIDGE_EPEX:
		ivb_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	case CPU_HASWELL_EPEX:
		hsw_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	case CPU_BROADWELL_DE:
		bdw_de_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	case CPU_BROADWELL_EPEX:
		bdw_epex_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	case CPU_SKYLAKE_XEON:
		skylake_s_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	case CPU_ICELAKE_XEON:
	case CPU_ICELAKE_DE:
	case CPU_TREMONT_D:
		i10nm_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	case CPU_SAPPHIRERAPIDS:
	case CPU_EMERALDRAPIDS:
		sapphire_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	case CPU_DENVERTON:
		denverton_decode_model(cputype, log->bank, log->status, log->misc);
		break;
	}
}

char *intel_bank_name(int num)
{
	static char bname[64];
	sprintf(bname, "BANK %d", num);
	return bname;
}
