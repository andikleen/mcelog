/* Copyright (c) 2026, Zhaoxin, Inc.
 *
 * Zhaoxin KH-50000 machine check decoding for mcelog.
 * Adapted from rasdaemon's mce-zhaoxin-kh50000.c.
 *
 * Licensed under the GNU General Public License, version 2.
 *
 * Author: Lyle Li <LyleLi-oc@zhaoxin.com>
 */

#include <stdio.h>
#include "mcelog.h"
#include "bitfield.h"
#include "zhaoxin-kh50000.h"
#include "yellow.h"

/* Zhaoxin KH-50000 CPU Error Bank */
#define MCE_CPU_BANK		0x0
#define MCE_PL2_CACHE_BANK	0x1
#define MCE_CRMCA0_PCIE_BANK	0x2
#define MCE_CRMCA0_IOD_ZDI_BANK	0x3
#define MCE_SVID0_BANK		0x4
#define MCE_CRMCA1_IOD_ZDI_BANK	0x5
#define MCE_CRMCA1_PCIE_BANK	0x6
#define MCE_CCD_ZDI_BANK	0x7
#define MCE_DRAM_BANK0		0x9
#define MCE_DRAM_BANK1		0xA
#define MCE_DRAM_BANK2		0xB
#define MCE_DRAM_BANK3		0xC
#define MCE_DRAM_BANK4		0xD
#define MCE_DRAM_BANK5		0xE
#define MCE_DRAM_BANK6		0xF
#define MCE_DRAM_BANK7		0x10
#define MCE_LLC_CACHE_BANK	0x11
#define MCE_DRAM_BANK8		0x12
#define MCE_DRAM_BANK9		0x13
#define MCE_DRAM_BANK10		0x14
#define MCE_DRAM_BANK11		0x15
#define MCE_SVID2_BANK		0x16
#define MCE_ZPI_BANK		0x1A
#define MCE_SVID1_BANK		0x1B
#define MCE_HIF_H0_BANK		0x1C
#define MCE_HIF_H1_BANK		0x1D
#define MCE_HIF_H2_BANK		0x1E
#define MCE_HIF_H3_BANK		0x1F

#define IOD_ZDI_DEV_COUNT	6
#define ZPI_DEV_COUNT		3

static char *bank_name_str[] = {
	"CPU Error Bank",
	"PL2 Cache Error Bank",
	"CRMCA0 PCIE Error Bank",
	"CRMCA0 IOD ZDI Error Bank",
	"SVID0 Error Bank",
	"CRMCA1 IOD ZDI Error Bank",
	"CRMCA1 PCIE Error Bank",
	"CCD ZDI Error Bank",
	"Reserved Error Bank",
	"Memory Error Bank9",
	"Memory Error Bank10",
	"Memory Error Bank11",
	"Memory Error Bank12",
	"Memory Error Bank13",
	"Memory Error Bank14",
	"Memory Error Bank15",
	"Memory Error Bank16",
	"LLC Cache Error Bank",
	"Memory Error Bank18",
	"Memory Error Bank19",
	"Memory Error Bank20",
	"Memory Error Bank21",
	"SVID2 Error Bank",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"Reserved Error Bank",
	"ZPI Error Bank",
	"SVID1 Error Bank",
	"HIF H0 Error Bank",
	"HIF H1 Error Bank",
	"HIF H2 Error Bank",
	"HIF H3 Error Bank",
};

static char *cpu_error_str[] = {
	"Unknown error",
	"Unknown error",
	"Machine hung error",
	"Undefined ucode address error",
};

static struct numfield error_coreid[] = {
	NUMBERFORCE(23, 24, "siod_id"),
	NUMBERFORCE(19, 22, "ccd_id"),
	NUMBERFORCE(16, 18, "core_id"),
	{}
};

static struct field cpu_error_fields[] = {
	FIELD(25, cpu_error_str),
	{}
};

static char *cache_error_str[] = {
	"Unknown Error",
	"ECC single bit error for data part in the same line",
	"ECC single bit error for different line",
	"ECC multi bit error for data part",
};

static struct field pl2_cache_error_fields[] = {
	FIELD(24, cache_error_str),
	{}
};

static struct field llc_cache_error_fields[] = {
	FIELD(25, cache_error_str),
	{}
};

static char *pcie_error_str[] = {
	"Fatal error",
	"Non-fatal error",
	"Correctable error",
};

static struct field pcie_error_fields[] = {
	FIELD(16, pcie_error_str),
	{}
};

static char *iod_zdi_zpi_error_str[] = {
	"Unknown error",
	"Receiver overflow status error(TL)",
	"Flow control protocol error(TL)",
	"Surprise down error",
	"Data link protocol error(DLL)",
	"Replay timer timeout error(DLL)",
	"REPLAY_NUM Rollover error(DLL)",
	"Bad data link layer packet error(DLL)",
	"Bad transaction layer packet error(DLL)",
	"Receiver error(PHY)",
	"PHY training error(PHY)",
	"Link-width down-mode due to link unreliable",
	"Unknown error",
	"Unknown error",
	"Link-speed down-mode due to link unreliable",
	"Unknown error",
	"X32X24 Link-width down-mode due to link unreliable",
	"X16X12 Link-width down-mode due to link unreliable",
	"X8 Link-width down-mode due to link unreliable",
	"X4 Link-width down-mode due to link unreliable",
	"X2 Link-width down-mode due to link unreliable",
	"GEN4 Link-speed down-mode due to link unreliable",
	"GEN3 Link-speed down-mode due to link unreliable",
	"GEN2 Link-speed down-mode due to link unreliable",
};

static struct field iod_zdi_zpi_error_fields[] = {
	FIELD(16, iod_zdi_zpi_error_str),
	{}
};

static char *ccd_zdi_error_str[] = {
	"Unknown error",
	"Receive overflow error",
	"PHY training error",
	"FC protocol error",
	"Surprise down error",
	"DLLM protocol error",
	"DLLM replay timeout error",
	"DLLM replay number rollover report",
	"Bad DLLP error",
	"Bad TLP error",
	"Gen2 unreliable error",
	"Gen3 unreliable error",
	"Gen4 unreliable error",
	"X2 unreliable error",
	"X4 unreliable error",
	"X8 unreliable error",
	"X16/X12 unreliable error",
	"X32/X24 unreliable error",
};

static struct field ccd_zdi_error_fields[] = {
	FIELD(25, ccd_zdi_error_str),
	{}
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
	{}
};

static const char * const mem_error_mccod_str[] = {
	"Generic undefined request error",
	"Memory read error",
	"Memory write error",
	"Address/Command error",
	"Memory scrubbing error",
	"data poison enable, Error source is dramc, master normal read",
	"data poison enable, Error source is dramc, patrol read",
	"key hit error",
};

static const char * const mem_channel_str[] = {
	"channel A0",
	"channel B0",
	"channel A1",
	"channel B1",
	"channel C0",
	"channel C1",
	"channel A2",
	"channel B2",
	"channel C2",
	"channel A3",
	"channel B3",
	"channel C3",
};

static char *mem_specific_error_str[] = {
	"Unknown error",
	"Single bit ECC error",
	"Multiple bit ECC error",
	"Command parity error",
	"CRC error",
	"Parity error retry failed",
	"CRC error retry failed",
	"CPUIF CHA0 DVAD decode error(reserved)",
	"CPUIF CHB0 DVAD decode error(reserved)",
	"CPUIF CHA1 DVAD decode error(reserved)",
	"CPUIF CHB1 DVAD decode error(reserved)",
	"MCUTRF DVAD decode error(reserved)",
	"GMINT CHA0 DVAD decode error(reserved)",
	"GMINT CHB0 DVAD decode error(reserved)",
	"GMINT CHA1 DVAD decode error(reserved)",
	"GMINT CHB1 DVAD decode error(reserved)",
	"Key not hit error",
};

static struct field mem_specific_error_fields[] = {
	FIELD(16, mem_specific_error_str),
	{}
};


static char *hif_error_str[] = {
	"Unknown error",
	"HIF dvad error",
	"SNT multi bit ecc error",
	"SNT single bit ecc error",
	"CXL decpoison uc error",
	"CXL decpoison ce error",
	"CXL parity error",
};

static struct field hif_error_fields[] = {
	FIELD(16, hif_error_str),
	{}
};

char *kh50000_bank_name(unsigned int bank)
{
	static char numeric[32];

	if (bank < NELE(bank_name_str))
		return bank_name_str[bank];
	snprintf(numeric, sizeof(numeric), "BANK %u", bank);
	return numeric;
}

static void decode_cpu_error(struct mce *m)
{
	decode_numfield(m->status, error_coreid);
	decode_bitfield(m->status, cpu_error_fields);
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
		decode_bitfield(m->status, pl2_cache_error_fields);
		level = 2;
		level_str = "Level-2";
		break;
	case MCE_LLC_CACHE_BANK:
		decode_numfield(m->status, error_coreid);
		Wprintf("LLC cache: ");
		decode_bitfield(m->status, llc_cache_error_fields);
		level = 3;
		level_str = "Level-3";
		break;
	default:
		return;
	}

	/*
	 * KH-50000 reports PL2/LLC errors with the architectural MCACOD 0x1
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

static void decode_pcie_error(struct mce *m)
{
	/*
	 * The BDF in MISC is already decoded by the generic IO MCA
	 * (0x0e0b) handling, only add the model specific severity.
	 */
	Wprintf("PCIe: ");
	decode_bitfield(m->status, pcie_error_fields);
}

static void decode_zdi_zpi_error(struct mce *m)
{
	unsigned int devnum;
	int idx;

	switch (m->bank) {
	case MCE_CRMCA0_IOD_ZDI_BANK:
	case MCE_CRMCA1_IOD_ZDI_BANK:
		Wprintf("IOD_ZDI: ");
		if (m->status & MCI_STATUS_MISCV) {
			devnum = EXTRACT(m->misc, 25, 30);
			for (idx = 0; idx < IOD_ZDI_DEV_COUNT; idx++)
				if (test_prefix(idx, devnum))
					break;
			if (idx < IOD_ZDI_DEV_COUNT)
				Wprintf("zdi%d ", idx);
		}
		decode_bitfield(m->status, iod_zdi_zpi_error_fields);
		break;
	case MCE_CCD_ZDI_BANK:
		decode_numfield(m->status, error_coreid);
		Wprintf("CCD_ZDI: ");
		decode_bitfield(m->status, ccd_zdi_error_fields);
		break;
	case MCE_ZPI_BANK:
		Wprintf("ZPI: ");
		if (m->status & MCI_STATUS_MISCV) {
			devnum = EXTRACT(m->misc, 25, 27);
			for (idx = 0; idx < ZPI_DEV_COUNT; idx++)
				if (test_prefix(idx, devnum))
					break;
			if (idx < ZPI_DEV_COUNT)
				Wprintf("zpi%d ", idx);
		}
		decode_bitfield(m->status, iod_zdi_zpi_error_fields);
		break;
	default:
		break;
	}
}

static void decode_svid_error(struct mce *m)
{
	const char *svid_str;

	switch (m->bank) {
	case MCE_SVID0_BANK:
		svid_str = "svid0";
		break;
	case MCE_SVID1_BANK:
		svid_str = "svid1";
		break;
	case MCE_SVID2_BANK:
		svid_str = "svid2";
		break;
	default:
		svid_str = "svid";
		break;
	}

	Wprintf("SVID: %s ", svid_str);
	if (m->status & MCI_STATUS_MISCV)
		Wprintf("vrm %u ", (unsigned int)EXTRACT(m->misc, 24, 25));
	decode_bitfield(m->status, svid_error_fields);
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
	Wprintf("Model specific (KH-50000): ");
	if (mca_err_code == 0x1)
		Wprintf("DVAD Error, ");
	else if (mca_err_code & (1 << 7)) {
		unsigned int channel_code = EXTRACT(mca_err_code, 0, 3);
		unsigned int mem_err_code = EXTRACT(mca_err_code, 4, 6);

		/* Map the numeric channel to the KH-50000 channel name */
		if (channel_code < NELE(mem_channel_str))
			Wprintf("%s, ", mem_channel_str[channel_code]);

		/* 0-4 are architectural and already decoded generically */
		if (mem_err_code >= 5 && mem_err_code < NELE(mem_error_mccod_str))
			Wprintf("%s, ", mem_error_mccod_str[mem_err_code]);
	}

	Wprintf("Internal Error Code: ");
	decode_bitfield(m->status, mem_specific_error_fields);

	if (m->status & MCI_STATUS_MISCV) {
		Wprintf("CRC/PAR MRP Log: 0x%llx\n",
			(unsigned long long)EXTRACT(m->misc, 31, 63));
		Wprintf("Rank: %llu\n", (unsigned long long)EXTRACT(m->misc, 25, 26));
		Wprintf("ECC ERROR Syndrome Single Bit: 0x%llx\n",
			(unsigned long long)EXTRACT(m->misc, 9, 16));
		Wprintf("Addr Mode: %s\n",
			EXTRACT(m->misc, 6, 8) == 2 ? "SVA" : "Unknown");
	}
}

static void decode_hif_error(struct mce *m)
{
	const char *hif_str;

	switch (m->bank) {
	case MCE_HIF_H0_BANK:
		hif_str = "hif0";
		break;
	case MCE_HIF_H1_BANK:
		hif_str = "hif1";
		break;
	case MCE_HIF_H2_BANK:
		hif_str = "hif2";
		break;
	case MCE_HIF_H3_BANK:
		hif_str = "hif3";
		break;
	default:
		hif_str = "hif";
		break;
	}

	Wprintf("HIF: %s ", hif_str);
	decode_bitfield(m->status, hif_error_fields);
}

void kh50000_decode_model(struct mce *m)
{
	switch (m->bank) {
	case MCE_CPU_BANK:
		decode_cpu_error(m);
		break;
	case MCE_PL2_CACHE_BANK:
	case MCE_LLC_CACHE_BANK:
		decode_cache_error(m);
		break;
	case MCE_CRMCA0_PCIE_BANK:
	case MCE_CRMCA1_PCIE_BANK:
		decode_pcie_error(m);
		break;
	case MCE_CRMCA0_IOD_ZDI_BANK:
	case MCE_CRMCA1_IOD_ZDI_BANK:
	case MCE_CCD_ZDI_BANK:
	case MCE_ZPI_BANK:
		decode_zdi_zpi_error(m);
		break;
	case MCE_SVID0_BANK:
	case MCE_SVID1_BANK:
	case MCE_SVID2_BANK:
		decode_svid_error(m);
		break;
	case MCE_DRAM_BANK0:
	case MCE_DRAM_BANK1:
	case MCE_DRAM_BANK2:
	case MCE_DRAM_BANK3:
	case MCE_DRAM_BANK4:
	case MCE_DRAM_BANK5:
	case MCE_DRAM_BANK6:
	case MCE_DRAM_BANK7:
	case MCE_DRAM_BANK8:
	case MCE_DRAM_BANK9:
	case MCE_DRAM_BANK10:
	case MCE_DRAM_BANK11:
		decode_mem_error(m);
		break;
	case MCE_HIF_H0_BANK:
	case MCE_HIF_H1_BANK:
	case MCE_HIF_H2_BANK:
	case MCE_HIF_H3_BANK:
		decode_hif_error(m);
		break;
	default:
		break;
	}
}

void kh50000_memerr_misc(struct mce *m, int *channel)
{
	if (m->bank < 9 || m->bank > 21 || m->bank == 17)
		*channel = -1;
}
