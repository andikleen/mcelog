/* Copyright (c) 2026, Zhaoxin, Inc.
 *
 * Zhaoxin KH-40000 machine check decoding for mcelog.
 *
 * Licensed under the GNU General Public License, version 2.
 *
 * Original Author: Marcelo <marcelo@zhaoxin.com>
 * Ported by: Lyle Li <LyleLi-oc@zhaoxin.com>
 */

#include <stdio.h>
#include "mcelog.h"
#include "bitfield.h"
#include "zhaoxin-kh40000.h"
#include "yellow.h"

#define MCE_CPU_BANK		0x0
#define MCE_PL2_CACHE_BANK	0x1
#define MCE_SVID_BANK		0x4
#define MCE_ZPI_BANK		0x5
#define MCE_PCIE_BANK		0x6
#define MCE_DRAM_BANK0		0x9
#define MCE_DRAM_BANK1		0xA
#define MCE_LLC_CACHE_BANK	0x11

static char *bank_name_str[] = {
	"CPU Error Bank",
	"PL2 Cache Error Bank",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"SVID Error Bank",
	"ZPI Error Bank",
	"PCIE Error Bank",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"Memory Error Bank9",
	"Memory Error Bank10",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"LLC Cache Error Bank",
};

static char *cpu_error_str[] = {
	"Unknown Error",
	"Thermal Trip",
	"Machine Hung",
	"Undefined Ucode Address",
};

static struct field cpu_error_fields[] = {
	FIELD(24, cpu_error_str),
	{},
};

static struct numfield error_coreid[] = {
	NUMBERFORCE(16, 17, "Core id"),
	NUMBERFORCE(18, 19, "Cluster id"),
	NUMBERFORCE(20, 22, "Subnode id"),
	NUMBERFORCE(23, 23, "Socket id"),
	{},
};

static char *cache_error_str[] = {
	"Unknown Error",
	"ECC single bit for data part in the same line",
	"ECC single bit for different line",
	"ECC multi bit for data part",
};

static struct field cache_error_fields[] = {
	FIELD(24, cache_error_str),
	{},
};

static char *svid_error_str[] = {
	"No error",
	"SVID Resend fail error",
	"VRM Over current error",
	"VRM Over temp error",
	"VRM Parity error",
};

static struct field svid_error_fields[] = {
	FIELD(24, svid_error_str),
	{},
};

static char *zpi_error_str[] = {
	"Unknown error",
	"Receiver Overflow Status",
	"Flow Control Protocol Error Status",
	"Surprise Down Error Status",
	"Data Link Protocol Error Status",
	"Replay Timer Timeout Error Status",
	"REPLAY_NUM Rollover Status",
	"Bad Data Link Layer Packet Status",
	"Bad TLP Status",
	"Receiver Error Status",
	"Unsupported/Undefined Packet",
	"Link-Width down-mode due to Link Unreliable",
	"Link-Speed down-mode due to Link Unreliable",
};

static struct field zpi_error_fields[] = {
	FIELD(16, zpi_error_str),
	{},
};

static char *zpi_dev_flag_str[] = {
	"opi0",
	"opi1(reserved)",
	"zpi",
	"unknown",
};

static char *pcie_error_str[] = {
	"Fatal error",
	"Non-fatal error",
	"Correctable error",
};

static struct field pcie_error_fields[] = {
	FIELD(16, pcie_error_str),
	{},
};


static char *mem_specific_error_str[] = {
	"Unknown error",
	"Single bit ECC error",
	"Multiple bit ECC error",
	"command parity error",
	"CRC error",
	"Parity error retry failed",
	"CRC error retry failed",
	"CPUIF DVAD decode error",
	"MCUTRF DVAD decode error",
};

static struct field mem_specific_fields[] = {
	FIELD(16, mem_specific_error_str),
	{},
};

static char *mem_addmod_str[] = {
	"Unknown",
	"Unknown",
	"DRAMC",
	"DVAD",
};

char *kh40000_bank_name(unsigned int bank)
{
	static char numeric[32];

	if (bank < NELE(bank_name_str))
		return bank_name_str[bank];
	snprintf(numeric, sizeof(numeric), "BANK %u", bank);
	return numeric;
}

static void decode_cpu_error(struct mce *m)
{
	if (EXTRACT(m->status, 30, 31) == 0x1) {
		Wprintf("CPU: ");
		decode_bitfield(m->status, cpu_error_fields);
		decode_numfield(m->status, error_coreid);
	}
}

static void decode_cache_error(struct mce *m)
{
	unsigned int level, cpu;
	char *level_str = NULL;

	if (EXTRACT(m->status, 30, 31) != 0x1)
		return;

	switch (m->bank) {
	case MCE_PL2_CACHE_BANK:
		Wprintf("PL2 cache: ");
		level = 2;
		level_str = "Level-2";
		break;
	case MCE_LLC_CACHE_BANK:
		Wprintf("LLC cache: ");
		level = 3;
		level_str = "Level-3";
		break;
	default:
		return;
	}

	decode_bitfield(m->status, cache_error_fields);
	decode_numfield(m->status, error_coreid);

	/*
	 * KH-40000 reports PL2/LLC errors with the architectural MCACOD 0x1
	 * ("Unclassified Error"). The generic decoder therefore cannot
	 * identify them as cache errors and does not run the cache threshold
	 * trigger. Handle the model-specific yellow threshold here, and keep
	 * this path limited to MCACOD 0x1 so architectural cache errors handled
	 * by the generic decoder do not invoke the trigger twice.
	 */
	if (!(m->mcgcap & MCG_TES_P) ||
	    (m->status & MCI_STATUS_UC) ||
	    EXTRACT(m->status, 0, 15) != 0x1 ||
	    EXTRACT(m->status, 53, 54) != 0x2)
		return;

	cpu = m->extcpu ? m->extcpu : m->cpu;
	run_yellow_trigger(cpu, 2, level, "Generic", level_str, m->socketid);
}

static void decode_svid_error(struct mce *m)
{
	Wprintf("SVID: ");
	if (m->status & MCI_STATUS_MISCV)
		Wprintf("vrm %u ", (unsigned int)EXTRACT(m->misc, 24, 25));
	decode_bitfield(m->status, svid_error_fields);
}

static void decode_zpi_error(struct mce *m)
{
	Wprintf("ZPI: ");
	if (m->status & MCI_STATUS_MISCV)
		Wprintf("%s ", zpi_dev_flag_str[EXTRACT(m->misc, 25, 26)]);
	decode_bitfield(m->status, zpi_error_fields);
}
static void decode_pcie_error(struct mce *m)
{
	/*
	 * The BDF in MISC is already decoded by the generic IO MCA
	 * (0x0e0b) handling, only add the model specific severity.
	 */
	Wprintf("PCIe: ");
	decode_bitfield(m->status, pcie_error_fields);
}

static void decode_mem_error(struct mce *m)
{
	unsigned int mca_err_code = EXTRACT(m->status, 0, 15);

	/*
	 * Model specific additions on a single line. The architectural
	 * 1MMM CCCC encoding is decoded and accounted (memdb/page
	 * offlining) by the generic Zhaoxin layer.
	 * DVAD (0x1) is clarified here because the generic layer reports
	 * it as "Unclassified Error"; Parity (0x5) is already reported
	 * generically as "Internal parity error".
	 */
	Wprintf("Model specific (KH-40000): ");
	if (mca_err_code == 0x1)
		Wprintf("DVAD Error, ");

	Wprintf("Internal Error Code: ");
	decode_bitfield(m->status, mem_specific_fields);

	if (m->status & MCI_STATUS_MISCV) {
		unsigned int addmod = EXTRACT(m->misc, 6, 8);

		Wprintf("CRC/PAR MRP Log: 0x%llx\n",
			(unsigned long long)EXTRACT(m->misc, 31, 63));
		Wprintf("Rank: %llu\n", (unsigned long long)EXTRACT(m->misc, 25, 26));
		Wprintf("ECC ERROR Syndrome Single Bit: 0x%llx\n",
			(unsigned long long)EXTRACT(m->misc, 9, 16));
		Wprintf("Addr Mode: %s\n",
			addmod < NELE(mem_addmod_str) ? mem_addmod_str[addmod] : "Unknown");
	}
}

void kh40000_decode_model(struct mce *m)
{
	switch (m->bank) {
	case MCE_CPU_BANK:
		decode_cpu_error(m);
		break;
	case MCE_PL2_CACHE_BANK:
	case MCE_LLC_CACHE_BANK:
		decode_cache_error(m);
		break;
	case MCE_SVID_BANK:
		decode_svid_error(m);
		break;
	case MCE_ZPI_BANK:
		decode_zpi_error(m);
		break;
	case MCE_PCIE_BANK:
		decode_pcie_error(m);
		break;
	case MCE_DRAM_BANK0:
	case MCE_DRAM_BANK1:
		decode_mem_error(m);
		break;
	}
}

void kh40000_memerr_misc(struct mce *m, int *channel)
{
	if (m->bank < 9 || m->bank > 10)
		*channel = -1;
}
