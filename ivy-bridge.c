/* Copyright (C) 2013 Intel Corporation
   Decode Intel Ivy Bridge specific machine check errors.

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
#include "ivy-bridge.h"
#include "memdb.h"

/* See IA32 SDM Vol3B Table 16-17 */

static char *pcu_1[] = {
	[0] = "No error",
	[1] = "Non_IMem_Sel",
	[2] = "I_Parity_Error",
	[3] = "Bad_OpCode",
	[4] = "I_Stack_Underflow",
	[5] = "I_Stack_Overflow",
	[6] = "D_Stack_Underflow",
	[7] = "D_Stack_Overflow",
	[8] = "Non-DMem_Sel",
	[9] = "D_Parity_Error"
};

static char *pcu_2[] = { 
	[0x00] = "No Error",
	[0x0D] = "MC_IMC_FORCE_SR_S3_TIMEOUT",
	[0x0E] = "MC_MC_CPD_UNCPD_ST_TIMEOUT",
	[0x0F] = "MC_PKGS_SAFE_WP_TIMEOUT",
	[0x43] = "MC_PECI_MAILBOX_QUIESCE_TIMEOUT",
	[0x44] = "MC_CRITICAL_VR_FAILED",
	[0x45] = "MC_ICC_MAX-NOTSUPPORTED",
	[0x5C] = "MC_MORE_THAN_ONE_LT_AGENT",
	[0x60] = "MC_INVALID_PKGS_REQ_PCH",
	[0x61] = "MC_INVALID_PKGS_REQ_QPI",
	[0x62] = "MC_INVALID_PKGS_RES_QPI",
	[0x63] = "MC_INVALID_PKGC_RES_PCH",
	[0x64] = "MC_INVALID_PKG_STATE_CONFIG",
	[0x70] = "MC_WATCHDG_TIMEOUT_PKGC_SECONDARY",
	[0x71] = "MC_WATCHDG_TIMEOUT_PKGC_MAIN",
	[0x72] = "MC_WATCHDG_TIMEOUT_PKGS_MAIN",
	[0x7A] = "MC_HA_FAILSTS_CHANGE_DETECTED",
	[0x7B] = "MC_PCIE_R2PCIE-RW_BLOCK_ACK_TIMEOUT",
	[0x81] = "MC_RECOVERABLE_DIE_THERMAL_TOO_HOT",
};

static struct field pcu_mc4[] = { 
	FIELD(16, pcu_1),
	FIELD(24, pcu_2),
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

void ivb_decode_model(int cputype, int bank, u64 status, u64 misc)
{
	switch (bank) { 
	case 4:
		Wprintf("PCU: ");
		decode_bitfield(status, pcu_mc4);
		Wprintf("\n");
		break;
	case 5:
		if (cputype == CPU_IVY_BRIDGE_EPEX) {
			/* MCACOD already decoded */
			Wprintf("QPI\n");
		}
		break;
	case 9: case 10: case 11: case 12:
	case 13: case 14: case 15: case 16:
		Wprintf("MemCtrl: ");
		decode_bitfield(status, memctrl_mc9);
		Wprintf("\n");
		break;
	}
}

/*
 * Ivy Bridge EP and EX processors (family 6, model 62) support additional
 * logging for corrected errors in the integrated memory controller (IMC)
 * banks. The mode is off by default, but can be enabled by setting the
 * "MemError Log Enable" * bit in MSR_ERROR_CONTROL (MSR 0x17f).
 * The SDM leaves it as an exercise for the reader to convert the
 * faling rank to a DIMM slot.
 */
static int failrank2dimm(unsigned failrank, int socket, int channel)
{
	switch (failrank) {
	case 0: case 1: case 2: case 3:
		return 0;
	case 4: case 5:
		return 1;
	case 6: case 7:
		if (get_memdimm(socket, channel, 2, 0))
			return 2;
		else
			return 1;
	}
	return -1;
}

void ivy_bridge_ep_memerr_misc(struct mce *m, int *channel, int *dimm)
{
	u64 status = m->status;
	unsigned	failrank, chan;

	/* Ignore unless this is an corrected extended error from an iMC bank */
	if (!imc_log || m->bank < 9 || m->bank > 16 || (status & MCI_STATUS_UC) ||
		!test_prefix(7, status & 0xefff))
		return;

	chan = EXTRACT(status, 0, 3);
	if (chan == 0xf)
		return;

	if (EXTRACT(m->misc, 62, 62)) {
		failrank = EXTRACT(m->misc, 46, 50);
		dimm[0] = failrank2dimm(failrank, m->socketid, chan);
		channel[0] = chan;
	}
	if (EXTRACT(m->misc, 63, 63)) {
		failrank = EXTRACT(m->misc, 51, 55);
		dimm[1] = failrank2dimm(failrank, m->socketid, chan);
		channel[1] = chan;
	}
}
