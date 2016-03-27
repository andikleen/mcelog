/* Based on K8 decoding code written for the 2.4 kernel by Andi Kleen and 
 * Eric Morton. Hacked and extended for mcelog by AK.
 *
 * Original copyright: 
 * K8 parts Copyright 2002,2003 Andi Kleen, SuSE Labs.
 * Additional K8 decoding and simplification Copyright 2003 Eric Morton, Newisys Inc 
 * K8 threshold counters decoding Copyright 2005,2006 Jacob Shin, AMD Inc.
 * 
 * Subject to the GNU General Public License
 */

#include <stdio.h>
#include "mcelog.h"
#include "k8.h"

static char *k8bank[] = {
	"data cache",
	"instruction cache",
	"bus unit",
	"load/store unit",
	"northbridge",
	"fixed-issue reoder"
};
static char *transaction[] = { 
	"instruction", "data", "generic", "reserved"
}; 
static char *cachelevel[] = { 
	"0", "1", "2", "generic"
};
static char *memtrans[] = { 
	"generic error", "generic read", "generic write", "data read",
	"data write", "instruction fetch", "prefetch", "evict", "snoop",
	"?", "?", "?", "?", "?", "?", "?"
};
static char *partproc[] = { 
	"local node origin", "local node response", 
	"local node observed", "generic participation"
};
static char *timeout[] = { 
	"request didn't time out",
	"request timed out"
};
static char *memoryio[] = { 
	"memory", "res.", "i/o", "generic"
};
static char *nbextendederr[] = { 
	"RAM ECC error", 
	"CRC error",
	"Sync error",
	"Master abort",
	"Target abort",
	"GART error",
	"RMW error",
	"Watchdog error",
	"RAM Chipkill ECC error", 
	"DEV Error",
	"Link Data Error",
	"Link Protocol Error",
	"NB Array Error",
	"DRAM Parity Error",
	"Link Retry",
	"Tablew Walk Data Error",
	"L3 Cache Data Error",
	"L3 Cache Tag Error",
	"L3 Cache LRU Error"
};
static char *highbits[32] = { 
	[31] = "valid",
	[30] = "error overflow (multiple errors)",
	[29] = "error uncorrected",
	[28] = "error enable",
	[27] = "misc error valid",
	[26] = "error address valid", 
	[25] = "processor context corrupt", 
	[24] = "res24",
	[23] = "res23",
	/* 22-15 ecc syndrome bits */
	[14] = "corrected ecc error",
	[13] = "uncorrected ecc error",
	[12] = "res12",
	[11] = "L3 subcache in error bit 1",
	[10] = "L3 subcache in error bit 0",
	[9] = "sublink or DRAM channel",
	[8] = "error found by scrub", 
	/* 7-4 ht link number of error */ 
	[3] = "err cpu3",
	[2] = "err cpu2",
	[1] = "err cpu1",
	[0] = "err cpu0",
};
static char *k8threshold[] = {
	[0 ... K8_MCELOG_THRESHOLD_DRAM_ECC - 1] = "Unknown threshold counter",
	[K8_MCELOG_THRESHOLD_DRAM_ECC] = "MC4_MISC0 DRAM threshold",
	[K8_MCELOG_THRESHOLD_LINK] = "MC4_MISC1 Link threshold",
	[K8_MCELOG_THRESHOLD_L3_CACHE] = "MC4_MISC2 L3 Cache threshold",
	[K8_MCELOG_THRESHOLD_FBDIMM] = "MC4_MISC3 FBDIMM threshold",
	[K8_MCELOG_THRESHOLD_FBDIMM + 1 ... 
	 K8_MCE_THRESHOLD_TOP - K8_MCE_THRESHOLD_BASE - 1] = 
                "Unknown threshold counter",
};


static void decode_k8_generic_errcode(u64 status)
{
	unsigned short errcode = status & 0xffff;
	int i;

	for (i=0; i<32; i++) {
		if (i==31 || i==28 || i==26)
			continue;
		if (highbits[i] && (status & (1ULL<<(i+32)))) {
			Wprintf( "       bit%d = %s\n", i+32, highbits[i]);
		}
	}

	if ((errcode & 0xFFF0) == 0x0010) {
		Wprintf( "  TLB error '%s transaction, level %s'\n",
		       transaction[(errcode >> 2) & 3],
		       cachelevel[errcode & 3]);
	}
	else if ((errcode & 0xFF00) == 0x0100) {
		Wprintf( "  memory/cache error '%s mem transaction, %s transaction, level %s'\n",
		       memtrans[(errcode >> 4) & 0xf],
		       transaction[(errcode >> 2) & 3],
		       cachelevel[errcode & 3]);
	}
	else if ((errcode & 0xF800) == 0x0800) {
		Wprintf( "  bus error '%s, %s\n             %s mem transaction\n             %s access, level %s'\n",
		       partproc[(errcode >> 9) & 0x3],
		       timeout[(errcode >> 8) & 1],
		       memtrans[(errcode >> 4) & 0xf],
		       memoryio[(errcode >> 2) & 0x3],
		       cachelevel[(errcode & 0x3)]);
	}
}

static void decode_k8_dc_mc(u64 status, int *err)
{
	unsigned short exterrcode = (status >> 16) & 0x0f;
	unsigned short errcode = status & 0xffff;

	if(status&(3ULL<<45)) {
		Wprintf( "  Data cache ECC error (syndrome %x)",
		       (u32) (status >> 47) & 0xff);
		if(status&(1ULL<<40)) {
			Wprintf(" found by scrubber");
		}
		Wprintf("\n");
	}

	if ((errcode & 0xFFF0) == 0x0010) {
		Wprintf( "  TLB parity error in %s array\n",
		       (exterrcode == 0) ? "physical" : "virtual");
	}

	decode_k8_generic_errcode(status);
}

static void decode_k8_ic_mc(u64 status, int *err)
{
	unsigned short exterrcode = (status >> 16) & 0x0f;
	unsigned short errcode = status & 0xffff;

	if(status&(3ULL<<45)) {
		Wprintf("  Instruction cache ECC error\n");
	}

	if ((errcode & 0xFFF0) == 0x0010) {
		Wprintf("  TLB parity error in %s array\n",
		       (exterrcode == 0) ? "physical" : "virtual");
	}

	decode_k8_generic_errcode(status);
}

static void decode_k8_bu_mc(u64 status, int *err)
{
	unsigned short exterrcode = (status >> 16) & 0x0f;

	if(status&(3ULL<<45)) {
		Wprintf("  L2 cache ECC error\n");
	}

	Wprintf("  %s array error\n",
	       (exterrcode == 0) ? "Bus or cache" : "Cache tag");

	decode_k8_generic_errcode(status);
}

static void decode_k8_ls_mc(u64 status, int *err)
{
	decode_k8_generic_errcode(status);
}

static void decode_k8_nb_mc(u64 status, int *memerr)
{
	unsigned short exterrcode = (status >> 16) & 0x0f;

	Wprintf("  Northbridge %s\n", nbextendederr[exterrcode]);

	switch (exterrcode) { 
	case 0:
		*memerr = 1;
		Wprintf("  ECC syndrome = %x\n",
		       (u32) (status >> 47) & 0xff);
		break;
	case 8:	
		*memerr = 1;
		Wprintf("  Chipkill ECC syndrome = %x\n",
		       (u32) ((((status >> 24) & 0xff) << 8) | ((status >> 47) & 0xff)));
		break;
	case 1: 
	case 2:
	case 3:
	case 4:
	case 6:
		Wprintf("  link number = %x\n",
		       (u32) (status >> 36) & 0xf);
		break;		   
	}

	decode_k8_generic_errcode(status);
}

static void decode_k8_fr_mc(u64 status, int *err)
{
	decode_k8_generic_errcode(status);
}

static void decode_k8_threshold(u64 misc)
{	
	if (misc & MCI_THRESHOLD_OVER)
		Wprintf("  Threshold error count overflow\n");
}

typedef void (*decoder_t)(u64, int *ismemerr); 

static decoder_t decoders[] = { 
	[0] = decode_k8_dc_mc,
	[1] = decode_k8_ic_mc,
	[2] = decode_k8_bu_mc,
	[3] = decode_k8_ls_mc,
	[4] = decode_k8_nb_mc,
	[5] = decode_k8_fr_mc,
}; 

void decode_k8_mc(struct mce *mce, int *ismemerr)
{
	if (mce->bank < NELE(decoders))
		decoders[mce->bank](mce->status, ismemerr);
	else if (mce->bank >= K8_MCE_THRESHOLD_BASE &&
		 mce->bank < K8_MCE_THRESHOLD_TOP)
		decode_k8_threshold(mce->misc);
	else
		Wprintf("  no decoder for unknown bank %u\n", mce->bank);
}

char *k8_bank_name(unsigned num)
{ 
	static char buf[64];
	char *s = "unknown";
	if (num < NELE(k8bank))
		s = k8bank[num];
	else if (num >= K8_MCE_THRESHOLD_BASE && 
		 num < K8_MCE_THRESHOLD_TOP)
		s = k8threshold[num - K8_MCE_THRESHOLD_BASE];
	buf[sizeof(buf)-1] = 0;
	snprintf(buf, sizeof(buf) - 1, "%u %s", num, s);
	return buf;
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
