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
#include "sandy-bridge.h"

/* decode mce for P4/Xeon and Core2 family */

static inline int test_prefix(int nr, __u32 value)
{
	return ((value >> nr) == 1);
}

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
	if (ll > NELE(LL)) {
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

static void decode_mca(__u32 mca, u64 track, int cpu, int *ismemerr, int socket)
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

	static char *msg[] = {
		[0] = "No Error",
		[1] = "Unclassified",
		[2] = "Microcode ROM parity error",
		[3] = "External error",
		[4] = "FRC error",
	};

	if (mca & (1UL << 12)) {
		Wprintf("corrected filtering (some unreported errors in same region)\n");
		mca &= ~(1UL << 12);
	}

	if (mca < NELE(msg)) {
		Wprintf("%s\n", msg[mca]); 
		return;
	}

	if ((mca >> 2) == 3) { 
		Wprintf("%s Generic memory hierarchy error\n", get_LL_str(mca & 3));
	} else if (test_prefix(4, mca)) {
		Wprintf("%s TLB %s Error\n",
				get_TT_str((mca & TLB_TT_MASK) >> TLB_TT_SHIFT),
				get_LL_str((mca & TLB_LL_MASK) >> 
					    TLB_LL_SHIFT));
	} else if (test_prefix(8, mca)) {
		unsigned typenum = (mca & CACHE_TT_MASK) >> CACHE_TT_SHIFT;
		unsigned levelnum = (mca & CACHE_LL_MASK) >> CACHE_LL_SHIFT;
		char *type = get_TT_str(typenum);
		char *level = get_LL_str(levelnum);
		Wprintf("%s CACHE %s %s Error\n", type, level,
				get_RRRR_str((mca & CACHE_RRRR_MASK) >> 
					      CACHE_RRRR_SHIFT));
		if (track == 2)
			run_yellow_trigger(cpu, typenum, levelnum, type, level, socket);
	} else if (test_prefix(10, mca)) {
		if (mca == 0x400)
			Wprintf("Internal Timer error\n");
		else
			Wprintf("Internal unclassified error: %x\n", mca & 0xffff);
	} else if (test_prefix(11, mca)) {
		Wprintf("BUS %s %s %s %s %s Error\n",
				get_LL_str((mca & BUS_LL_MASK) >> BUS_LL_SHIFT),
				get_PP_str((mca & BUS_PP_MASK) >> BUS_PP_SHIFT),
				get_RRRR_str((mca & BUS_RRRR_MASK) >> 
					      BUS_RRRR_SHIFT),
				get_II_str((mca & BUS_II_MASK) >> BUS_II_SHIFT),
				get_T_str((mca & BUS_T_MASK) >> BUS_T_SHIFT));
	} else if (test_prefix(7, mca)) {
		decode_memory_controller(mca);
		*ismemerr = 1;
	} else 
		Wprintf("Unknown Error %x\n", mca);
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

static void decode_mci(__u64 status, int cpu, unsigned mcgcap, int *ismemerr,
		       int socket)
{
	u64 track = 0;

	Wprintf("MCi status:\n");
	if (!(status & MCI_STATUS_VAL))
		Wprintf("Machine check not valid\n");

	if (status & MCI_STATUS_OVER)
		Wprintf("Error overflow\n");
	
	if (status & MCI_STATUS_UC) 
		Wprintf("Uncorrected error\n");
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

	if ((mcgcap == 0 || (mcgcap & MCG_TES_P)) && !(status & MCI_STATUS_UC)) {
		track = (status >> 53) & 3;
		decode_tracking(track);
	}
	Wprintf("MCA: ");
	decode_mca(status & 0xffffL, track, cpu, ismemerr, socket);
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
		return;
	}

	decode_mcg(log->mcgstatus);
	decode_mci(log->status, cpu, log->mcgcap, ismemerr, socket);

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
		snb_decode_model(cputype, log->bank, log->status, size);
		break;
	}
}

char *intel_bank_name(int num)
{
	static char bname[64];
	sprintf(bname, "BANK %d", num);
	return bname;
}
