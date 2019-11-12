/* Copyright (C) 2019 Intel Corporation
   Decode Intel 10nm specific machine check errors.

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
#include "i10nm.h"
#include "memdb.h"

/* Memory error was corrected by mirroring with channel failover */
#define I10NM_MCI_MISC_FO      (1ULL<<63)
/* Memory error was corrected by mirroring and primary channel scrubbed successfully */
#define I10NM_MCI_MISC_MC      (1ULL<<62)

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
	[0x58] = "MCA_WATCHDOG_TIMEOUT_PKGC_SLAVE",
	[0x59] = "MCA_WATCHDOG_TIMEOUT_PKGC_MASTER",
	[0x5A] = "MCA_WATCHDOG_TIMEOUT_PKGS_MASTER",
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
	[0x00] = "Phy Initialization Failure (NumInit)",
	[0x01] = "Phy Detected Drift Buffer Alarm",
	[0x02] = "Phy Detected Latency Buffer Rollover",
	[0x10] = "LL Rx detected CRC error: unsuccessful LLR (entered Abort state)",
	[0x11] = "LL Rx Unsupported/Undefined packet",
	[0x12] = "LL or Phy Control Error",
	[0x13] = "LL Rx Parameter Exception",
	[0x1F] = "LL Detected Control Error",
	[0x20] = "Phy Initialization Abort",
	[0x21] = "Phy Inband Reset",
	[0x22] = "Phy Lane failure, recovery in x8 width",
	[0x23] = "Phy L0c error corrected without Phy reset",
	[0x24] = "Phy L0c error triggering Phy reset",
	[0x25] = "Phy L0p exit error corrected with reset",
	[0x30] = "LL Rx detected CRC error: successful LLR without Phy Reinit",
	[0x31] = "LL Rx detected CRC error: successful LLR with Phy Reinit",
	[0x32] = "Tx received LLR",
};

static struct field upi2[] = {
	FIELD(0, upi_2),
	{}
};

static struct field m2m[] = {
	SBITFIELD(16, "MC read data error"),
	SBITFIELD(17, "Reserved"),
	SBITFIELD(18, "MC partial write data error"),
	SBITFIELD(19, "Full write data error"),
	SBITFIELD(20, "M2M clock-domain-crossing buffer (BGF) error"),
	SBITFIELD(21, "M2M time out"),
	SBITFIELD(22, "M2M tracker parity error"),
	SBITFIELD(23, "fatal Bucket1 error"),
	{}
};

static char *imc_0[] = {
	[0x01] = "Address parity error",
	[0x02] = "Data parity error",
	[0x03] = "Data ECC error",
	[0x04] = "Data byte enable parity error",
	[0x05] = "Received uncorrectable data",
	[0x06] = "Received uncorrectable metadata",
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
	[0x03] = "RPA parity error",
	[0x04] = "RPA parity error",
	[0x05] = "WPA parity error",
	[0x06] = "DDR_T_DPPP data BE error",
	[0x07] = "DDR_T_DPPP data error",
	[0x08] = "DDR link failure",
	[0x11] = "PCLS CAM error",
	[0x12] = "PCLS data error",
};

static char *imc_2[] = {
	[0x00] = "DDR4 command / address parity error",
	[0x20] = "HBM command / address parity error",
	[0x21] = "HBM data parity error",
};

static char *imc_4[] = {
	[0x00] = "RPQ parity (primary) error",
	[0x01] = "RPQ parity (buddy) error",
	[0x04] = "WPQ parity (primary) error",
	[0x05] = "WPQ parity (buddy) error",
	[0x08] = "RPB parity (primary) error",
	[0x09] = "RPB parity (buddy) error",
};

static char *imc_8[] = {
	[0x00] = "DDR-T bad request",
	[0x01] = "DDR Data response to an invalid entry",
	[0x02] = "DDR data response to an entry not expecting data",
	[0x03] = "DDR4 completion to an invalid entry",
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

void i10nm_decode_model(int cputype, int bank, u64 status, u64 misc)
{
	u64 f;

	switch (bank) {
	case 4:
		Wprintf("PCU: ");
		f = EXTRACT(status, 24, 31);
		if (f)
			decode_bitfield(f, pcu1);
		f = EXTRACT(status, 20, 23);
		if (f)
			decode_bitfield(f, pcu2);
		f = EXTRACT(status, 16, 19);
		if (f)
			decode_bitfield(f, pcu3);
		break;

	case 5:
	case 7:
	case 8:
		Wprintf("UPI: ");
		f = EXTRACT(status, 22, 31);
		if (f)
			decode_bitfield(status, upi1);
		f = EXTRACT(status, 16, 21);
		decode_bitfield(f, upi2);
		break;

	case 12:
	case 16:
	case 20:
	case 24:
		Wprintf("M2M: ");
		f = EXTRACT(status, 24, 25);
		Wprintf("MscodDDRType=0x%llx\n", f);
		f = EXTRACT(status, 26, 31);
		Wprintf("MscodMiscErrs=0x%llx\n", f);
		decode_bitfield(status, m2m);
		break;

	case 13:
	case 14:
	case 15:
	case 17:
	case 18:
	case 19:
	case 21:
	case 22:
	case 23:
	case 25:
	case 26:
	case 27:
		Wprintf("MemCtrl: ");
		f = EXTRACT(status, 16, 23);
		switch (EXTRACT(status, 24, 31)) {
		case 0: decode_bitfield(f, imc0); break;
		case 1: decode_bitfield(f, imc1); break;
		case 2: decode_bitfield(f, imc2); break;
		case 4: decode_bitfield(f, imc4); break;
		case 8: decode_bitfield(f, imc8); break;
		}
		break;
	}
}

int i10nm_ce_type(int bank, u64 status, u64 misc)
{
	if (bank != 12 && bank != 16 && bank != 20 && bank != 24)
		return 0;

	if (status & MCI_STATUS_MISCV) {
		if (misc & I10NM_MCI_MISC_FO)
			return 1;
		if (misc & I10NM_MCI_MISC_MC)
			return 2;
	}

	return 0;
}

/*
 * There isn't enough information to identify the DIMM. But
 * we can derive the channel from the bank number.
 * There can be four memory controllers with two channels each.
 */
void i10nm_memerr_misc(struct mce *m, int *channel, int *dimm)
{
	u64 status = m->status;
	unsigned int chan, imc;

	/* Check this is a memory error */
	if (!test_prefix(7, status & 0xefff))
		return;

	chan = EXTRACT(status, 0, 3);
	if (chan == 0xf)
		return;

	switch (m->bank) {
	case 12: /* M2M 0 */
	case 13: /* IMC 0, Channel 0 */
	case 14: /* IMC 0, Channel 1 */
	case 15: /* IMC 0, Channel 2 */
		imc = 0;
		break;
	case 16: /* M2M 1 */
	case 17: /* IMC 1, Channel 0 */
	case 18: /* IMC 1, Channel 1 */
	case 19: /* IMC 1, Channel 2 */
		imc = 1;
		break;
	case 20: /* M2M 2 */
	case 21: /* IMC 2, Channel 0 */
	case 22: /* IMC 2, Channel 1 */
	case 23: /* IMC 2, Channel 2 */
		imc = 2;
		break;
	case 24: /* M2M 3 */
	case 25: /* IMC 3, Channel 0 */
	case 26: /* IMC 3, Channel 1 */
	case 27: /* IMC 3, Channel 2 */
		imc = 3;
		break;
	default:
		return;
	}

	channel[0] = imc * 3 + chan;
}
