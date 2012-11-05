/* Copyright (C) 2010 Intel Corporation
   Decode Intel Sandy Bridge specific machine check errors.

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

   Author: Andi Kleen 
*/

#include "mcelog.h"
#include "bitfield.h"
#include "sandy-bridge.h"
#include "memdb.h"

/* See IA32 SDM Vol3B Appendix E.4 ff */

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
	[0x20] = "MC_RCLK_PLL_LOCK_TIMEOUT",
	[0x21] = "MC_PCIE_PLL_LOCK_TIMEOT",
	[0x22] = "MC_BOOT_VID_SET_TIMEOUT",
	[0x23] = "MC_BOOT_FREQUENCY_SET_TIMEOUT",
	[0x24] = "MC_START_IA_CORES_TIMEOUT",
	[0x26] = "MC_PCIE_RCOMP_TIMEOUT",
	[0x27] = "MC_PMA_DNS_COMMAND_TIMEOUT",
	[0x28] = "MC_MESSAGE_CHANNEL_TIMEOUT",
	[0x29] = "MC_GVFSM_BGF_PROGRAM_TIMEOUT",
	[0x2A] = "MC_MC_PLL_LOCK_TIMEOUT",
	[0x2B] = "MC_MS_BGF_PROGRAM_TIMEOUT",
};

static struct field pcu_mc4[] = { 
	FIELD(16, pcu_1),
	FIELD(24, pcu_2),
	{}
};

void snb_decode_model(int cputype, int bank, u64 status, u64 misc)
{
	switch (bank) { 
	case 4:
		Wprintf("PCU: ");
		decode_bitfield(status, pcu_mc4);
		Wprintf("\n");
		break;
	case 6:
	case 7:
		if (cputype == CPU_SANDY_BRIDGE_EP) {
			/* MCACOD already decoded */
			Wprintf("QPI\n");
		}
		break;
	}
}

/*
 * Sandy Bridge EP and EP4S processors (family 6, model 45) support additional
 * logging for corrected errors in the integrated memory controller (IMC)
 * banks. The mode is off by default, but can be enabled by setting the
 * "MemError Log Enable" * bit in MSR_ERROR_CONTROL (MSR 0x17f).
 * The documentation in the August 2012 edition of Intel's Software developer
 * manual has some minor errors because the worng version of table 16-16
 * "Intel IMC MC Error Codes for IA32_MCi_MISC (i= 8, 11)" was included.
 * Corrections are:
 *  Bit 62 is the "VALID" bit for the "first-device" bits in MISC and STATUS
 *  Bit 63 is the "VALID" bit for the "second-device" bits in MISC
 *  Bits 58:56 and 61:59 should be marked as "reserved".
 * There should also be a footnote explaining how the "failing rank" fields
 * can be converted to a DIMM number within a channel for systems with either
 * two or three DIMMs per channel.
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

void sandy_bridge_ep_memerr_misc(struct mce *m, int *channel, int *dimm)
{
	u64 status = m->status;
	unsigned	failrank, chan;

	/* Ignore unless this is an corrected extended error from an iMC bank */
	if (!imc_log || m->bank < 8 || m->bank > 11 || (status & MCI_STATUS_UC) ||
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
