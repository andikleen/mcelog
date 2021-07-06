/* Copyright (C) 2015 Intel Corporation
   Decode Intel Broadwell D specific machine check errors.

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
#include "broadwell_de.h"
#include "memdb.h"

/* See IA32 SDM Vol3B Table 16-24 */

static char *pcu_1[] = {
	[0x00] = "No Error",
	[0x09] = "MC_MESSAGE_CHANNEL_TIMEOUT",
	[0x13] = "MC_DMI_TRAINING_TIMEOUT",
	[0x15] = "MC_DMI_CPU_RESET_ACK_TIMEOUT",
	[0x1E] = "MC_VR_ICC_MAX_LT_FUSED_ICC_MAX",
	[0x25] = "MC_SVID_COMMAN_TIMEOUT",
	[0x26] = "MCA_PKGC_DIRECT_WAKE_RING_TIMEOUT",
	[0x29] = "MC_VR_VOUT_MAC_LT_FUSED_SVID",
	[0x2B] = "MC_PKGC_WATCHDOG_HANG_CBZ_DOWN",
	[0x2C] = "MC_PKGC_WATCHDOG_HANG_CBZ_UP",
	[0x44] = "MC_CRITICAL_VR_FAILED",
	[0x46] = "MC_VID_RAMP_DOWN_FAILED",
	[0x49] = "MC_SVID_WRITE_REG_VOUT_MAX_FAILED",
	[0x4B] = "MC_BOOT_VID_TIMEOUT_DRAM_0",
	[0x4F] = "MC_SVID_COMMAND_ERROR",
	[0x52] = "MC_FIVR_CATAS_OVERVOL_FAULT",
	[0x53] = "MC_FIVR_CATAS_OVERCUR_FAULT",
	[0x57] = "MC_SVID_PKGC_REQUEST_FAILED",
	[0x58] = "MC_SVID_IMON_REQUEST_FAILED",
	[0x59] = "MC_SVID_ALERT_REQUEST_FAILED",
	[0x62] = "MC_INVALID_PKGS_RSP_QPI",
	[0x64] = "MC_INVALID_PKG_STATE_CONFIG",
	[0x67] = "MC_HA_IMC_RW_BLOCK_ACK_TIMEOUT",
	[0x6A] = "MC_MSGCH_PMREQ_CMP_TIMEOUT",
	[0x72] = "MC_WATCHDOG_TIMEOUT_PKGS_MAIN",
	[0x81] = "MC_RECOVERABLE_DIE_THERMAL_TOO_HOT"
};

static struct field pcu_mc4[] = {
	FIELD(24, pcu_1),
	{}
};

/* See IA32 SDM Vol3B Table 16-18 */

static struct field memctrl_mc9[] = {
	SBITFIELD(16, "Address parity error"),
	SBITFIELD(17, "HA Wrt buffer Data parity error"),
	SBITFIELD(18, "HA Wrt byte enable parity error"),
	SBITFIELD(19, "Corrected patrol scrub error"),
	SBITFIELD(20, "Uncorrected patrol scrub error"),
	SBITFIELD(21, "Corrected spare error"),
	SBITFIELD(22, "Uncorrected spare error"),
	SBITFIELD(23, "Corrected memory read error"),
	SBITFIELD(24, "iMC, WDB, parity errors"),
	{}
};

void bdw_de_decode_model(int cputype, int bank, u64 status, u64 misc)
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
		if (EXTRACT(status, 16, 19) & 3)
			Wprintf("PCU internal error ");
		if (EXTRACT(status, 20, 23) & 4)
			Wprintf("Ubox error ");
		decode_bitfield(status, pcu_mc4);
		break;
	case 9: case 10:
		Wprintf("MemCtrl: ");
		decode_bitfield(status, memctrl_mc9);
		break;
	}
}
