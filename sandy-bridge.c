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
