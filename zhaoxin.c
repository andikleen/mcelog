/* Copyright (c) 2026, Zhaoxin, Inc.
 *
 * Zhaoxin CPU machine check decoding for mcelog.
 *
 * Licensed under the GNU General Public License, version 2.
 *
 * Author: Lyle Li <LyleLi-oc@zhaoxin.com>
 */

#include <stdio.h>
#include <stddef.h>
#include "mcelog.h"
#include "intel.h"
#include "bitfield.h"
#include "memdb.h"
#include "page.h"
#include "zhaoxin.h"
#include "zhaoxin-kh50000.h"
#include "yellow.h"
#include "bus.h"
#include "unknown.h"

/*
 * Generic architectural memory controller encoding.
 * 5-7 are model specific (decoded by the per-model decoder if defined).
 */
static const char * const mem_error_mccod_str[] = {
	"Generic undefined request error",
	"Memory read error",
	"Memory write error",
	"Address/Command error",
	"Memory scrubbing error",
	"Reserved 5",
	"Reserved 6",
	"Reserved 7",
};

enum cputype select_zhaoxin_cputype(int family, int model)
{
	/* Enable memdb; Zhaoxin tracks errors at channel granularity (no DIMM id). */
	memory_error_support = 1;

	if (family == 0x7) {
		switch (model) {
		case 0x7b:
			return CPU_ZHAOXIN_KH50000;
		}
	}
	Eprintf("Zhaoxin family %x model %x CPU: only generic decoding\n",
		family, model);
	return CPU_ZHAOXIN;
}

static int zhaoxin_memory_error(struct mce *m, unsigned int recordlen)
{
	u32 mca = m->status & 0xffff;

	/*
	 * Bit 12 is the corrected filtering bit. Mask it out before matching
	 * the memory error encoding.
	 */
	mca &= ~(1U << 12);
	if ((mca >> 7) == 1) {
		unsigned int corr_err_cnt = 0;
		int channel = (m->status & 0xf) == 0xf ? -1 : (int)(m->status & 0xf);

		switch (cputype) {
		case CPU_ZHAOXIN_KH50000:
			kh50000_memerr_misc(m, &channel);
			break;
		default:
			break;
		}

		/* Account at channel granularity (no DIMM id) into memdb. */
		if (recordlen > offsetof(struct mce, mcgcap) && (m->mcgcap & MCG_CMCI_P))
			corr_err_cnt = EXTRACT(m->status, 38, 52);
		memory_error(m, channel, -1, corr_err_cnt, recordlen);
		/* All memory errors participate in predictive page offlining. */
		account_page_error(m, channel, -1);
		return 1;
	}

	return 0;
}

/* No bugs known, but filter out memory errors if the user asked for it */
int mce_filter_zhaoxin(struct mce *m, unsigned int recordlen)
{
	if (zhaoxin_memory_error(m, recordlen))
		return !filter_memory_errors;
	return 1;
}

char *zhaoxin_bank_name(unsigned int num)
{
	static char numeric[64];

	switch (cputype) {
	case CPU_ZHAOXIN_KH50000:
		return kh50000_bank_name(num);
	default:
		snprintf(numeric, sizeof(numeric), "BANK %u", num);
		return numeric;
	}
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

static char *get_TT_str(__u8 t)
{
	static char * const TT[] = {
		"Instruction", "Data", "Generic", "Unknown"
	};

	if (t >= NELE(TT))
		return "UNKNOWN";

	return TT[t];
}

static char *get_LL_str(__u8 ll)
{
	static char * const LL[] = {
		"Level-0", "Level-1", "Level-2", "Level-3"
	};

	if (ll >= NELE(LL))
		return "UNKNOWN";

	return LL[ll];
}

static char *get_RRRR_str(__u8 rrrr)
{
	static const struct {
		__u8 value;
		char *str;
	} RRRR[] = {
		{0, "Generic"}, {1, "Read"},
		{2, "Write" }, {3, "Data-Read"},
		{4, "Data-Write"}, {5, "Instruction-Fetch"},
		{6, "Prefetch"}, {7, "Eviction"},
		{8, "Snoop"}
	};
	unsigned int i;

	for (i = 0; i < (int)NELE(RRRR); i++) {
		if (RRRR[i].value == rrrr)
			return RRRR[i].str;
	}

	return "UNKNOWN";
}

static char *get_PP_str(__u8 pp)
{
	static char * const PP[] = {
		"Local-CPU-originated-request",
		"Responded-to-request",
		"Observed-error-as-third-party",
		"Generic"
	};
	if (pp >= NELE(PP))
		return "UNKNOWN";

	return PP[pp];
}

static char *get_T_str(__u8 t)
{
	static char * const T[] = {
		"Request-did-not-timeout", "Request-timed-out"
	};

	if (t >= NELE(T))
		return "UNKNOWN";

	return T[t];
}

static char *get_II_str(__u8 i)
{
	static char * const II[] = {
		"Memory-access", "Reserved", "IO", "Other-transaction"
	};

	if (i >= NELE(II))
		return "UNKNOWN";

	return II[i];
}

static void decode_memory_error(struct mce *m)
{
	/* CCCC == 0xf means the channel is not specified */
	int channel = (m->status & 0xf) == 0xf ? -1 : (int)(m->status & 0xf);
	unsigned int mem_err_code = EXTRACT(m->status, 4, 6);
	char channel_buf[16];
	const char *channel_str;

	if (channel == -1)
		channel_str = "unspecified";
	else {
		snprintf(channel_buf, sizeof(channel_buf), "%d", channel);
		channel_str = channel_buf;
	}

	Wprintf("MCA Error Code: Memory Controller Channel %s %s\n",
		channel_str,
		mem_err_code < NELE(mem_error_mccod_str) ?
		mem_error_mccod_str[mem_err_code] : "Unknown error");
}

static int decode_mca(struct mce *m, u64 track, int socket, int cpu, int *ismemerr)
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
	static const char * const msg[] = {
		[0] = "No Error",
		[1] = "Unclassified Error",
		[2] = "Microcode ROM parity error",
		[3] = "External error",
		[4] = "FRC error",
		[5] = "Internal parity error",
		[6] = "SMM Handler Code Access Violation",
	};

	mca = m->status & 0xffff;
	if (mca & (1UL << 12)) {
		Wprintf("corrected filtering (some unreported errors in same region)\n");
		mca &= ~(1UL << 12);
	}

	/* Simple Error Codes */
	if (mca < NELE(msg)) {
		Wprintf("%s\n", msg[mca]);
		return ret;
	}

	if ((mca >> 2) == 3) {				/* Generic Cache Hierarchy Errors */
		unsigned int levelnum;
		char *level;

		levelnum = mca & 3;
		level = get_LL_str(levelnum);
		Wprintf("%s Generic cache hierarchy error\n", level);
		if (track == 2)
			run_yellow_trigger(cpu, -1, levelnum, "unknown", level, socket);
	} else if (test_prefix(4, mca)) {	/* TLB Errors */
		unsigned int levelnum, typenum;
		char *level, *type;

		typenum = (mca & TLB_TT_MASK) >> TLB_TT_SHIFT;
		type = get_TT_str(typenum);
		levelnum = (mca & TLB_LL_MASK) >> TLB_LL_SHIFT;
		level = get_LL_str(levelnum);
		Wprintf("%s TLB %s Error\n", type, level);
		if (track == 2)
			run_yellow_trigger(cpu, typenum, levelnum, type, level, socket);
	} else if (test_prefix(7, mca)) {	/* Memory Errors */
		decode_memory_error(m);
		*ismemerr = 1;
	} else if (test_prefix(8, mca)) {	/* Cache Hierarchy Errors */
		unsigned int typenum = (mca & CACHE_TT_MASK) >> CACHE_TT_SHIFT;
		unsigned int levelnum = ((mca & CACHE_LL_MASK) >> CACHE_LL_SHIFT) + 1;
		char *type = get_TT_str(typenum);
		char *level = get_LL_str(levelnum);

		Wprintf("%s CACHE %s %s Error\n", type, level,
				get_RRRR_str((mca & CACHE_RRRR_MASK) >>
					      CACHE_RRRR_SHIFT));
		if (track == 2)
			run_yellow_trigger(cpu, typenum, levelnum, type, level, socket);
	} else if (test_prefix(10, mca)) {	/* Internal Unclassified Errors */
		if (mca == 0x400)
			Wprintf("Internal Timer error\n");
		else if (mca == 0x402)
			Wprintf("Internal SVID error\n");
		else
			Wprintf("Internal unclassified error: %x\n", mca & 0xffff);

		ret = 1;
	} else if (test_prefix(11, mca)) {	/* Bus and Interconnect Errors */
		char *level, *pp, *rrrr, *ii, *timeout;

		level = get_LL_str((mca & BUS_LL_MASK) >> BUS_LL_SHIFT);
		pp = get_PP_str((mca & BUS_PP_MASK) >> BUS_PP_SHIFT);
		rrrr = get_RRRR_str((mca & BUS_RRRR_MASK) >> BUS_RRRR_SHIFT);
		ii = get_II_str((mca & BUS_II_MASK) >> BUS_II_SHIFT);
		timeout = get_T_str((mca & BUS_T_MASK) >> BUS_T_SHIFT);

		Wprintf("BUS error: %d %d %s %s %s %s %s\n", socket, cpu,
			level, pp, rrrr, ii, timeout);
		run_bus_trigger(socket, cpu, level, pp, rrrr, ii, timeout);

		if ((m->status & MCI_STATUS_MISCV) &&
		    (m->status & 0xefff) == 0x0e0b) {
			int	seg, bus, dev, fn;

			seg = EXTRACT(m->misc, 32, 39);
			bus = EXTRACT(m->misc, 24, 31);
			dev = EXTRACT(m->misc, 19, 23);
			fn = EXTRACT(m->misc, 16, 18);
			Wprintf("IO MCA reported by PCIe device %x:%02x:%02x.%x\n",
				seg, bus, dev, fn);
			run_iomca_trigger(socket, cpu, seg, bus, dev, fn);
		}
	} else {
		Wprintf("Unknown Error %x\n", mca);
		ret = 1;
	}
	return ret;
}

static int decode_mci(struct mce *m, int socket, int cpu, int *ismemerr)
{
	static char *arstate[4] = {
		[0] = "Uncorrected No Action Required (UCNA)",
		[1] = "Action Required (AR)",
		[2] = "Software Recoverable Action Optional (SRAO)",
		[3] = "Software Recoverable Action Required (SRAR)",
	};
	__u64 track = 0;

	Wprintf("MCi status:\n");
	if (!(m->status & MCI_STATUS_VAL))
		Wprintf("Machine check not valid\n");

	if (m->status & MCI_STATUS_OVER)
		Wprintf("Error overflow\n");

	if (m->status & MCI_STATUS_EN)
		Wprintf("Error enabled\n");
	if (m->status & MCI_STATUS_MISCV)
		Wprintf("MCi_MISC register valid\n");
	if (m->status & MCI_STATUS_ADDRV)
		Wprintf("MCi_ADDR register valid\n");

	if (m->status & MCI_STATUS_UC) {
		Wprintf("Uncorrected error\n");
		if (m->status & MCI_STATUS_PCC)
			Wprintf("Processor context corrupt\n");
		else if (m->status & (MCI_STATUS_S | MCI_STATUS_AR))
			Wprintf("%s\n", arstate[(m->status >> 55) & 3]);
	} else
		Wprintf("Corrected error\n");

	if ((m->mcgcap & MCG_TES_P) && !(m->status & MCI_STATUS_UC)) {
		track = (m->status >> 53) & 3;
		if (track == 1)
			Wprintf("Threshold based error status: green\n");
		else if (track == 2)
			Wprintf("Threshold based error status: yellow\n");
	}

	Wprintf("MCA: ");
	return decode_mca(m, track, socket, cpu, ismemerr);
}

void decode_zhaoxin_mc(struct mce *m, int cputype, int *ismemerr, unsigned int recordlen)
{
	int socket = recordlen > offsetof(struct mce, socketid) ? (int)m->socketid : -1;
	int cpu = m->extcpu ? m->extcpu : m->cpu;

	decode_mcg(m->mcgstatus);
	if (decode_mci(m, socket, cpu, ismemerr))
		run_unknown_trigger(socket, cpu, m);

	switch (cputype) {
	case CPU_ZHAOXIN_KH50000:
		kh50000_decode_model(m);
		break;
	default:
		break;
	}
}
