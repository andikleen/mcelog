/* Copyright (C) 2015 Intel Corporation
   Decode Intel Broadwell specific machine check errors.

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
#include "broadwell_epex.h"
#include "memdb.h"

/* Memory error was corrected by mirroring with channel failover */
#define BDW_MCI_MISC_FO      (1ULL<<41)
/* Memory error was corrected by mirroring and primary channel scrubbed successfully */
#define BDW_MCI_MISC_MC      (1ULL<<42)

/* See IA32 SDM Vol3B Table 16-20 */

static char *pcu_1[] = {
	[0x00] = "No Error",
	[0x09] = "MC_MESSAGE_CHANNEL_TIMEOUT",
	[0x0D] = "MC_IMC_FORCE_SR_S3_TIMEOUT",
	[0x0E] = "MC_CPD_UNCPD_SD_TIMEOUT",
	[0x13] = "MC_DMI_TRAINING_TIMEOUT",
	[0x15] = "MC_DMI_CPU_RESET_ACK_TIMEOUT",
	[0x1E] = "MC_VR_ICC_MAX_LT_FUSED_ICC_MAX",
	[0x25] = "MC_SVID_COMMAN_TIMEOUT",
	[0x29] = "MC_VR_VOUT_MAC_LT_FUSED_SVID",
	[0x2B] = "MC_PKGC_WATCHDOG_HANG_CBZ_DOWN",
	[0x2C] = "MC_PKGC_WATCHDOG_HANG_CBZ_UP",
	[0x39] = "MC_PKGC_WATCHDOG_HANG_C3_UP_SF",
	[0x44] = "MC_CRITICAL_VR_FAILED",
	[0x45] = "MC_ICC_MAX_NOTSUPPORTED",
	[0x46] = "MC_VID_RAMP_DOWN_FAILED",
	[0x47] = "MC_EXCL_MODE_NO_PMREQ_CMP",
	[0x48] = "MC_SVID_READ_REG_ICC_MAX_FAILED",
	[0x49] = "MC_SVID_WRITE_REG_VOUT_MAX_FAILED",
	[0x4B] = "MC_BOOT_VID_TIMEOUT_DRAM_0",
	[0x4C] = "MC_BOOT_VID_TIMEOUT_DRAM_1",
	[0x4D] = "MC_BOOT_VID_TIMEOUT_DRAM_2",
	[0x4E] = "MC_BOOT_VID_TIMEOUT_DRAM_3",
	[0x4F] = "MC_SVID_COMMAND_ERROR",
	[0x52] = "MC_FIVR_CATAS_OVERVOL_FAULT",
	[0x53] = "MC_FIVR_CATAS_OVERCUR_FAULT",
	[0x57] = "MC_SVID_PKGC_REQUEST_FAILED",
	[0x58] = "MC_SVID_IMON_REQUEST_FAILED",
	[0x59] = "MC_SVID_ALERT_REQUEST_FAILED",
	[0x60] = "MC_INVALID_PKGS_REQ_PCH",
	[0x61] = "MC_INVALID_PKGS_REQ_QPI",
	[0x62] = "MC_INVALID_PKGS_RSP_QPI",
	[0x63] = "MC_INVALID_PKGS_RSP_PCH",
	[0x64] = "MC_INVALID_PKG_STATE_CONFIG",
	[0x67] = "MC_HA_IMC_RW_BLOCK_ACK_TIMEOUT",
	[0x68] = "MC_IMC_RW_SMBUS_TIMEOUT",
	[0x69] = "MC_HA_FAILSTS_CHANGE_DETECTED",
	[0x6A] = "MC_MSGCH_PMREQ_CMP_TIMEOUT",
	[0x70] = "MC_WATCHDOG_TIMEOUT_PKGC_SECONDARY",
	[0x71] = "MC_WATCHDOG_TIMEOUT_PKGC_MAIN",
	[0x72] = "MC_WATCHDOG_TIMEOUT_PKGS_MAIN",
	[0x7C] = "MC_BIOS_RST_CPL_INVALID_SEQ",
	[0x7D] = "MC_MORE_THAN_ONE_TXT_AGENT",
	[0x81] = "MC_RECOVERABLE_DIE_THERMAL_TOO_HOT"
};

static struct field pcu_mc4[] = {
	FIELD(24, pcu_1),
	{}
};

/* See IA32 SDM Vol3B Table 16-21 */

static char *qpi[] = {
	[0x02] = "Intel QPI physical layer detected drift buffer alarm",
	[0x03] = "Intel QPI physical layer detected latency buffer rollover",
	[0x10] = "Intel QPI link layer detected control error from R3QPI",
	[0x11] = "Rx entered LLR abort state on CRC error",
	[0x12] = "Unsupported or undefined packet",
	[0x13] = "Intel QPI link layer control error",
	[0x15] = "RBT used un-initialized value",
	[0x20] = "Intel QPI physical layer detected a QPI in-band reset but aborted initialization",
	[0x21] = "Link failover data self healing",
	[0x22] = "Phy detected in-band reset (no width change)",
	[0x23] = "Link failover clock failover",
	[0x30] = "Rx detected CRC error - successful LLR after Phy re-init",
	[0x31] = "Rx detected CRC error - successful LLR without Phy re-init",
};

static struct field qpi_mc[] = {
	FIELD(16, qpi),
	{}
};

/* See IA32 SDM Vol3B Table 16-26 */

static struct field memctrl_mc9[] = {
	SBITFIELD(16, "DDR3 address parity error"),
	SBITFIELD(17, "Uncorrected HA write data error"),
	SBITFIELD(18, "Uncorrected HA data byte enable error"),
	SBITFIELD(19, "Corrected patrol scrub error"),
	SBITFIELD(20, "Uncorrected patrol scrub error"),
	SBITFIELD(21, "Corrected spare error"),
	SBITFIELD(22, "Uncorrected spare error"),
	SBITFIELD(24, "iMC write data buffer parity error"),
	SBITFIELD(25, "DDR4 command address parity error"),
	{}
};

void bdw_epex_decode_model(int cputype, int bank, u64 status, u64 misc)
{
	switch (bank) {
	case 4:
		Wprintf("PCU: ");
		switch (EXTRACT(status, 0, 15) & ~(1ull << 12)) {
		case 0x402: case 0x403:
			Wprintf("Internal errors ");
			break;
		case 0x406:
			Wprintf("Intel TXT errors ");
			break;
		case 0x407:
			Wprintf("Other UBOX Internal errors ");
			break;
		}
		if (EXTRACT(status, 16, 19))
			Wprintf("PCU internal error ");
		decode_bitfield(status, pcu_mc4);
		break;
	case 5:
	case 20:
	case 21:
		Wprintf("QPI: ");
		decode_bitfield(status, qpi_mc);
		break;
	case 9: case 10: case 11: case 12:
	case 13: case 14: case 15: case 16:
		Wprintf("MemCtrl: ");
		decode_bitfield(status, memctrl_mc9);
		break;
	}
}

/*
 * return: 0 - CE by normal ECC
 *         1 - CE by mirroring with channel failover
 *         2 - CE by mirroring and primary channel scrubbed successfully
 */
int bdw_epex_ce_type(int bank, u64 status, u64 misc)
{
	if (!(bank == 7 || bank == 8))
		return 0;

	if (status & MCI_STATUS_MISCV) {
		if (misc & BDW_MCI_MISC_FO)
			return 1;
		if (misc & BDW_MCI_MISC_MC)
			return 2;
	}

	return 0;
}
