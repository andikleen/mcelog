/* Copyright (C) 2023 Intel Corporation
   Decode Intel Xeon 4th generation specific machine check errors.

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

   Author: Tony Luck
*/

#include "mcelog.h"
#include "bitfield.h"
#include "sapphire.h"
#include "memdb.h"

static char *pcu_1[] = {
	[0x0D] = "MCA_LLC_BIST_ACTIVE_TIMEOUT",
	[0x0E] = "MCA_DMI_TRAINING_TIMEOUT",
	[0x0F] = "MCA_DMI_STRAP_SET_ARRIVAL_TIMEOUT",
	[0x10] = "MCA_DMI_CPU_RESET_ACK_TIMEOUT",
	[0x11] = "MCA_MORE_THAN_ONE_LT_AGENT",
	[0x14] = "MCA_INCOMPATIBLE_PCH_TYPE",
	[0x1E] = "MCA_BIOS_RST_CPL_INVALID_SEQ",
	[0x1F] = "MCA_BIOS_INVALID_PKG_STATE_CONFIG",
	[0x2D] = "MCA_PCU_PMAX_CALIB_ERROR",
	[0x2E] = "MCA_TSC100_SYNC_TIMEOUT",
	[0x3A] = "MCA_GPSB_TIMEOUT",
	[0x3B] = "MCA_PMSB_TIMEOUT",
	[0x3E] = "MCA_IOSFSB_PMREQ_CMP_TIMEOUT",
	[0x40] = "MCA_SVID_VCCIN_VR_ICC_MAX_FAILURE",
	[0x42] = "MCA_SVID_VCCIN_VR_VOUT_FAILURE",
	[0x43] = "MCA_SVID_CPU_VR_CAPABILITY_ERROR",
	[0x44] = "MCA_SVID_CRITICAL_VR_FAILED",
	[0x45] = "MCA_SVID_SA_ITD_ERROR",
	[0x46] = "MCA_SVID_READ_REG_FAILED",
	[0x47] = "MCA_SVID_WRITE_REG_FAILED",
	[0x4A] = "MCA_SVID_PKGC_REQUEST_FAILED",
	[0x4B] = "MCA_SVID_IMON_REQUEST_FAILED",
	[0x4C] = "MCA_SVID_ALERT_REQUEST_FAILED",
	[0x4D] = "MCA_SVID_MCP_VR_RAMP_ERROR",
	[0x56] = "MCA_FIVR_PD_HARDERR",
	[0x58] = "MCA_WATCHDOG_TIMEOUT_PKGC_SECONDARY",
	[0x59] = "MCA_WATCHDOG_TIMEOUT_PKGC_MAIN",
	[0x5A] = "MCA_WATCHDOG_TIMEOUT_PKGS_MAIN",
	[0x5B] = "MCA_WATCHDOG_TIMEOUT_MSG_CH_FSM",
	[0x5C] = "MCA_WATCHDOG_TIMEOUT_BULK_CR_FSM",
	[0x5D] = "MCA_WATCHDOG_TIMEOUT_IOSFSB_FSM",
	[0x60] = "MCA_PKGS_SAFE_WP_TIMEOUT",
	[0x61] = "MCA_PKGS_CPD_UNCPD_TIMEOUT",
	[0x62] = "MCA_PKGS_INVALID_REQ_PCH",
	[0x63] = "MCA_PKGS_INVALID_REQ_INTERNAL",
	[0x64] = "MCA_PKGS_INVALID_RSP_INTERNAL",
	[0x65 ... 0x7A] = "MCA_PKGS_RESET_PREP_TIMEOUT",
	[0x7B] = "MCA_PKGS_SMBUS_VPP_PAUSE_TIMEOUT",
	[0x7C] = "MCA_PKGS_SMBUS_MCP_PAUSE_TIMEOUT",
	[0x7D] = "MCA_PKGS_SMBUS_SPD_PAUSE_TIMEOUT",
	[0x80] = "MCA_PKGC_DISP_BUSY_TIMEOUT",
	[0x81] = "MCA_PKGC_INVALID_RSP_PCH",
	[0x83] = "MCA_PKGC_WATCHDOG_HANG_CBZ_DOWN",
	[0x84] = "MCA_PKGC_WATCHDOG_HANG_CBZ_UP",
	[0x87] = "MCA_PKGC_WATCHDOG_HANG_C2_BLKMASTER",
	[0x88] = "MCA_PKGC_WATCHDOG_HANG_C2_PSLIMIT",
	[0x89] = "MCA_PKGC_WATCHDOG_HANG_SETDISP",
	[0x8B] = "MCA_PKGC_ALLOW_L1_ERROR",
	[0x90] = "MCA_RECOVERABLE_DIE_THERMAL_TOO_HOT",
	[0xA0] = "MCA_ADR_SIGNAL_TIMEOUT",
	[0xA1] = "MCA_BCLK_FREQ_OC_ABOVE_THRESHOLD",
	[0xB0] = "MCA_DISPATCHER_RUN_BUSY_TIMEOUT",
	[0xC0] = "MCA_DISPATCHER_RUN_BUSY_TIMEOUT",
};

static char *pcu_2[] = {
	[0x04] = "Clock/power IP response timeout",
	[0x05] = "SMBus controller raised SMI",
	[0x09] = "PM controller received invalid transaction",
};

static char *pcu_3[] = {
	[0x01] = "Instruction address out of valid space",
	[0x02] = "Double bit RAM error on Instruction Fetch",
	[0x03] = "Invalid OpCode seen",
	[0x04] = "Stack Underflow",
	[0x05] = "Stack Overflow",
	[0x06] = "Data address out of valid space",
	[0x07] = "Double bit RAM error on Data Fetch",
};

static char *pcu_4[] = {
	[0x01] = "MCE when CR4.MCE is clear",
	[0x02] = "MCE when MCIP bit is set",
	[0x03] = "MCE under WPS",
	[0x04] = "Unrecoverable error during security flow execution",
	[0x05] = "SW triple fault shutdown",
	[0x06] = "VMX exit consistency check failures",
	[0x07] = "RSM consistency check failures",
	[0x08] = "Invalid conditions on protected mode SMM entry",
	[0x09] = "Unrecoverable error during security flow execution",
};

static struct field pcu1[] = {
	FIELD(0, pcu_1),
	{}
};

static struct field pcu2[] = {
	FIELD(0, pcu_2),
	{}
};

static struct field pcu3[] = {
	FIELD(0, pcu_3),
	{}
};

static struct field pcu4[] = {
	FIELD(0, pcu_4),
	{}
};

static struct field upi1[] = {
	SBITFIELD(22, "Phy Control Error"),
	SBITFIELD(23, "Unexpected Retry.Ack flit"),
	SBITFIELD(24, "Unexpected Retry.Req flit"),
	SBITFIELD(25, "RF parity error"),
	SBITFIELD(26, "Routeback Table error"),
	SBITFIELD(27, "Unexpected Tx Protocol flit (EOP, Header or Data)"),
	SBITFIELD(28, "Rx Header-or-Credit BGF credit overflow/underflow"),
	SBITFIELD(29, "Link Layer Reset still in progress when Phy enters L0"),
	SBITFIELD(30, "Link Layer reset initiated while protocol traffic not idle"),
	SBITFIELD(31, "Link Layer Tx Parity Error"),
	{}
};

static char *upi_2[] = {
	[0x00] = "Phy Initialization Failure",
	[0x01] = "Phy Detected Drift Buffer Alarm",
	[0x02] = "Phy Detected Latency Buffer Rollover",
	[0x10] = "LL Rx detected CRC error: unsuccessful LLR (entered Abort state)",
	[0x11] = "LL Rx Unsupported/Undefined packet",
	[0x12] = "LL or Phy Control Error",
	[0x13] = "LL Rx Parameter Exception",
	[0x15] = "UC LL Rx SGX MAC Error",
	[0x1F] = "LL Detected Control Error",
	[0x20] = "Phy Initialization Abort",
	[0x21] = "Phy Inband Reset",
	[0x22] = "Phy Lane failure, recovery in x8 width",
	[0x23] = "Phy L0c error corrected without Phy reset",
	[0x24] = "Phy L0c error triggering Phy reset",
	[0x25] = "Phy L0p exit error corrected with reset",
	[0x30] = "LL Rx detected CRC error: successful LLR without Phy Reinit",
	[0x31] = "LL Rx detected CRC error: successful LLR with Phy Reinit",
};

static struct field upi2[] = {
	FIELD(0, upi_2),
	{}
};

static char *m2m_0[] = {
	[0x01] = "Read ECC error",
	[0x02] = "Bucket1 error",
	[0x03] = "RdTrkr Parity error",
	[0x05] = "Prefetch channel mismatch",
	[0x07] = "Read completion parity error",
	[0x08] = "Response parity error",
	[0x09] = "Timeout error",
	[0x0A] = "CMI reserved credit pool error",
	[0x0B] = "CMI total credit count error",
	[0x0C] = "CMI credit oversubscription error",
};

static struct field m2m[] = {
	FIELD(0, m2m_0),
	{}
};

static char *imc_0[] = {
	[0x01] = "Address parity error",
	[0x02] = "Data parity error",
	[0x03] = "Data ECC error",
	[0x04] = "Data byte enable parity error",
	[0x07] = "Transaction ID parity error",
	[0x08] = "Corrected patrol scrub error",
	[0x10] = "Uncorrected patrol scrub error",
	[0x20] = "Corrected spare error",
	[0x40] = "Uncorrected spare error",
	[0x80] = "Corrected read error",
	[0xA0] = "Uncorrected read error",
	[0xC0] = "Uncorrected metadata",
};

static char *imc_1[] = {
	[0x00] = "WDB read parity error",
	[0x08] = "DDR link failure",
};

static char *imc_2[] = {
	[0x00] = "DDR5 command / address parity error",
};

static char *imc_4[] = {
	[0x00] = "RPQ parity (primary) error",
};

static char *imc_8[] = {
	[0x00] = "DDR-T bad request",
	[0x01] = "DDR Data response to an invalid entry",
	[0x02] = "DDR data response to an entry not expecting data",
	[0x03] = "DDR completion to an invalid entry",
	[0x04] = "DDR-T completion to an invalid entry",
	[0x05] = "DDR data/completion FIFO overflow",
	[0x06] = "DDR-T ERID correctable parity error",
	[0x07] = "DDR-T ERID uncorrectable error",
	[0x08] = "DDR-T interrupt received while outstanding interrupt was not ACKed",
	[0x09] = "ERID FI FO overflow",
	[0x0A] = "DDR-T error on FNV write credits",
	[0x0B] = "DDR-T error on FNV read credits",
	[0x0C] = "DDR-T scheduler error",
	[0x0D] = "DDR-T FNV error event",
	[0x0E] = "DDR-T FNV thermal event",
	[0x0F] = "CMI packet while idle",
	[0x10] = "DDR_T_RPQ_REQ_PARITY_ERR",
	[0x11] = "DDR_T_WPQ_REQ_PARITY_ERR",
	[0x12] = "2LM_NMFILLWR_CAM_ERR",
	[0x13] = "CMI_CREDIT_OVERSUB_ERR",
	[0x14] = "CMI_CREDIT_TOTAL_ERR",
	[0x15] = "CMI_CREDIT_RSVD_POOL_ERR",
	[0x16] = "DDR_T_RD_ERROR",
	[0x17] = "WDB_FIFO_ERR",
	[0x18] = "CMI_REQ_FIFO_OVERFLOW",
	[0x19] = "CMI_REQ_FIFO_UNDERFLOW",
	[0x1A] = "CMI_RSP_FIFO_OVERFLOW",
	[0x1B] = "CMI_RSP_FIFO_UNDERFLOW",
	[0x1C] = "CMI _MISC_MC_CRDT_ERRORS",
	[0x1D] = "CMI_MISC_MC_ARB_ERRORS",
	[0x1E] = "DDR_T_WR_CMPL_FI FO_OVERFLOW",
	[0x1F] = "DDR_T_WR_CMPL_FI FO_UNDERFLOW",
	[0x20] = "CMI_RD_CPL_FIFO_OVERFLOW",
	[0x21] = "CMI_RD_CPL_FIFO_UNDERFLOW",
	[0x22] = "TME_KEY_PAR_ERR",
	[0x23] = "TME_CMI_MISC_ERR",
	[0x24] = "TME_CMI_OVFL_ERR",
	[0x25] = "TME_CMI_UFL_ERR",
	[0x26] = "TME_TEM_SECURE_ERR",
	[0x27] = "TME_UFILL_PAR_ERR",
	[0x29] = "INTERNAL_ERR",
	[0x2A] = "TME_INTEGRITY_ERR",
	[0x2B] = "TME_TDX_ERR",
	[0x2C] = "TME_UFILL_TEM_SECURE_ERR",
	[0x2D] = "TME_KEY_POISON_ERR",
	[0x2E] = "TME_SECURITY_ENGINE_ERR",
};

static char *imc_10[] = {
	[0x08] = "CORR_PATSCRUB_MIRR2ND_ERR",
	[0x10] = "UC_PATSCRUB_MIRR2ND_ERR",
	[0x20] = "COR_SPARE_MIRR2ND_ERR",
	[0x40] = "UC_SPARE_MIRR2ND_ERR",
	[0x80] = "HA_RD_MIRR2ND_ERR",
	[0xA0] = "HA_UNCORR_RD_MIRR2ND_ERR",
};

static struct field imc0[] = {
	FIELD(0, imc_0),
	{}
};

static struct field imc1[] = {
	FIELD(0, imc_1),
	{}
};

static struct field imc2[] = {
	FIELD(0, imc_2),
	{}
};

static struct field imc4[] = {
	FIELD(0, imc_4),
	{}
};

static struct field imc8[] = {
	FIELD(0, imc_8),
	{}
};

static struct field imc10[] = {
	FIELD(0, imc_10),
	{}
};

static void sapphire_imc_misc(bool hbm, u64 status, u64 misc)
{
	u32 column = EXTRACT(misc, 9, 18) << 2;
	u32 row = EXTRACT(misc, 19, 39);
	u32 bank = EXTRACT(misc, 39, 40);
	u32 bankgroup = EXTRACT(misc, 37, 38) | (EXTRACT(misc, 41, 41) << 2);
	u32 fdevice = EXTRACT(misc, 43, 48);
	u32 hbm_fdevice = EXTRACT(misc, 51, 55);
	u32 subrank = EXTRACT(misc, 52, 55);
	u32 rank = EXTRACT(misc, 56, 58);
	u32 eccmode = EXTRACT(misc, 59, 62);
	u32 transient = EXTRACT(misc, 63, 63);

	Wprintf("bank: 0x%x bankgroup: 0x%x row: 0x%x column: 0x%x\n", bank, bankgroup, row, column);
	if (!transient && !EXTRACT(status, 61, 61)) {
		if (hbm)
			Wprintf("failed device: 0x%x,0x%x\n", hbm_fdevice, fdevice);
		else
			Wprintf("failed device: 0x%x\n", fdevice);
	}
	Wprintf("rank: 0x%x subrank: 0x%x\n", rank, subrank);
	if (hbm) {
		switch (eccmode) {
		case 1:
			Wprintf("HBM 64B read\n");
			break;
		case 9:
			Wprintf("HBM 32B read\n");
			break;
		}
	} else {
		Wprintf("ecc mode: ");
		switch (eccmode) {
		case 0: Wprintf("SDDC 2LM memory mode\n"); break;
		case 1: Wprintf("SDDC\n"); break;
		case 2: Wprintf("SDDC+1 2LM memory mode\n"); break;
		case 3: Wprintf("SDDC+1\n"); break;
		case 4: Wprintf("ADDDC 2LM memory mode\n"); break;
		case 5: Wprintf("ADDDC\n"); break;
		case 6: Wprintf("ADDDC+1 2LM memory mode\n"); break;
		case 7: Wprintf("ADDDC+1\n"); break;
		case 8: Wprintf("DDRT read\n"); break;
		default: Wprintf("unknown\n"); break;
		}
	}
	if (transient)
		Wprintf("transient\n");
}

enum banktype {
	BT_UNKNOWN,
	BT_PCU,
	BT_UPI,
	BT_M2M,
	BT_IMC,
	BT_HBMM2M,
	BT_HBMIMC,
};

static enum banktype sapphire[32] = {
	[4]		= BT_PCU,
	[5]		= BT_UPI,
	[12]		= BT_M2M,
	[13 ... 20]	= BT_IMC,
	[29]		= BT_HBMM2M,
	[30 ... 31]	= BT_HBMIMC,
};

void sapphire_decode_model(int cputype, int bank, u64 status, u64 misc)
{
	enum banktype banktype;
	u64 f;

	switch (cputype) {
	case CPU_SAPPHIRERAPIDS:
	case CPU_EMERALDRAPIDS:
		banktype = sapphire[bank];
		break;
	default:
		return;
	}

	switch (banktype) {
	case BT_UNKNOWN:
		break;

	case BT_PCU:
		Wprintf("PCU: ");
		f = EXTRACT(status, 24, 31);
		if (f)
			decode_bitfield(f, pcu1);
		f = EXTRACT(status, 20, 23);
		if (f)
			decode_bitfield(f, pcu2);
		f = EXTRACT(status, 16, 19);
		if (f) {
			if (EXTRACT(status, 0, 15) != 0x40C)
				decode_bitfield(f, pcu3);
			else
				decode_bitfield(f, pcu4);
		}
		break;

	case BT_UPI:
		Wprintf("UPI: ");
		f = EXTRACT(status, 22, 31);
		if (f)
			decode_bitfield(status, upi1);
		f = EXTRACT(status, 16, 21);
		decode_bitfield(f, upi2);
		break;

	case BT_HBMM2M:
		Wprintf("HBM: ");
		/*fallthrough*/

	case BT_M2M:
		Wprintf("M2M: ");
		f = EXTRACT(status, 24, 25);
		if (f == 1)
			Wprintf("HBM Error\n");
		f = EXTRACT(status, 16, 23);
		decode_bitfield(f, m2m);
		break;

	case BT_HBMIMC:
		Wprintf("HBM: ");
		/*fallthrough*/

	case BT_IMC:
		Wprintf("MemCtrl: ");
		f = EXTRACT(status, 16, 23);
		switch (EXTRACT(status, 24, 31)) {
		case 0: decode_bitfield(f, imc0); break;
		case 1: decode_bitfield(f, imc1); break;
		case 2: decode_bitfield(f, imc2); break;
		case 4: decode_bitfield(f, imc4); break;
		case 8: decode_bitfield(f, imc8); break;
		case 0x10: decode_bitfield(f, imc10); break;
		}
		sapphire_imc_misc(bank >= 30, status, misc);
		break;
	}
}

/*
 * There isn't enough information to identify the DIMM. But
 * we can derive the channel from the bank number.
 * There can be four memory controllers with two channels each.
 */
void sapphire_memerr_misc(struct mce *m, int *channel, int *dimm)
{
	u64 status = m->status;
	unsigned int chan;

	/* Check this is a memory error */
	if (!test_prefix(7, status & 0xefff))
		return;

	chan = EXTRACT(status, 0, 3);
	if (chan == 0xf)
		return;

	switch (m->bank) {
	case 13 ... 20:
		channel[0] = m->bank - 13;
		break;
	case 30 ... 31:
		channel[0] = 8 + m->bank - 30;
		break;
	}
}
