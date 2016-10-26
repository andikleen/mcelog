/* Copyright (C) 2008 Intel Corporation
   Decode Intel Nehalem specific machine check errors.

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

#include <string.h>
#include <stdio.h>
#include "mcelog.h"
#include "nehalem.h"
#include "bitfield.h"
#include "memdb.h"

/* See IA32 SDM Vol3B Appendix E.3.2 ff */

/* MC1_STATUS error */
static struct field qpi_status[] = {
	SBITFIELD(16, "QPI header had bad parity"),
	SBITFIELD(17, "QPI Data packet had bad parity"),
	SBITFIELD(18, "Number of QPI retries exceeded"),
	SBITFIELD(19, "Received QPI data packet that was poisoned by sender"),
	SBITFIELD(20, "QPI reserved 20"),
	SBITFIELD(21, "QPI reserved 21"),
	SBITFIELD(22, "QPI received unsupported message encoding"),
	SBITFIELD(23, "QPI credit type is not supported"),
	SBITFIELD(24, "Sender sent too many QPI flits to the receiver"),
	SBITFIELD(25, "QPI Sender sent a failed response to receiver"),
	SBITFIELD(26, "Clock jitter detected in internal QPI clocking"),
	{}
}; 

static struct field qpi_misc[] = {
	SBITFIELD(14, "QPI misc reserved 14"),
	SBITFIELD(15, "QPI misc reserved 15"),
	SBITFIELD(24, "QPI Interleave/Head Indication Bit (IIB)"),
	{}
};

static struct numfield qpi_numbers[] = {
	HEXNUMBER(0, 7, "QPI class and opcode of packet with error"),
	HEXNUMBER(8, 13, "QPI Request Transaction ID"),
	NUMBERFORCE(16, 18, "QPI Requestor/Home Node ID (RHNID)"),
	HEXNUMBER(19, 23, "QPI miscreserved 19-23"),
	{},
};

static struct field nhm_memory_status[] = {
	SBITFIELD(16, "Memory read ECC error"),
	SBITFIELD(17, "Memory ECC error occurred during scrub"),
	SBITFIELD(18, "Memory write parity error"),
	SBITFIELD(19, "Memory error in half of redundant memory"),
	SBITFIELD(20, "Memory reserved 20"),
	SBITFIELD(21, "Memory access out of range"),
	SBITFIELD(22, "Memory internal RTID invalid"), 
	SBITFIELD(23, "Memory address parity error"),
	SBITFIELD(24, "Memory byte enable parity error"),
	{}
};

static struct numfield nhm_memory_status_numbers[] = {
	HEXNUMBER(25, 37, "Memory MISC reserved 25..37"),
	NUMBERFORCE(38, 52, "Memory corrected error count (CORE_ERR_CNT)"),
	HEXNUMBER(53, 56, "Memory MISC reserved 53..56"),
	{}
};

static struct numfield nhm_memory_misc_numbers[] = {
	HEXNUMBERFORCE(0, 7, "Memory transaction Tracker ID (RTId)"),
	NUMBERFORCE(16, 17, "Memory DIMM ID of error"),
	NUMBERFORCE(18, 19, "Memory channel ID of error"),
	HEXNUMBERFORCE(32, 63, "Memory ECC syndrome"),
	{}
};

static char *internal_errors[] = {
	[0x0]  = "No Error",
	[0x3]  = "Reset firmware did not complete",
	[0x8]  = "Received an invalid CMPD",
	[0xa]  = "Invalid Power Management Request",
	[0xd]  = "Invalid S-state transition",
	[0x11] = "VID controller does not match POC controller selected",
	[0x1a] = "MSID from POC does not match CPU MSID",
};

static struct field internal_error_status[] = {
	FIELD(24, internal_errors),
	{}
};

static struct numfield internal_error_numbers[] = { 
	HEXNUMBER(16, 23, "Internal machine check status reserved 16..23"),
	HEXNUMBER(32, 56, "Internal machine check status reserved 32..56"),
	{},
};

/* Generic architectural memory controller encoding */

static char *mmm_mnemonic[] = { 
	"GEN", "RD", "WR", "AC", "MS", "RES5", "RES6", "RES7" 
};
static char *mmm_desc[] = { 
	"Generic undefined request",
	"Memory read error",
	"Memory write error",
	"Address/Command error",
	"Memory scrubbing error",
	"Reserved 5",
	"Reserved 6",
	"Reserved 7"
};

void decode_memory_controller(u32 status, u8 bank)
{
	char channel[30];
	if ((status & 0xf) == 0xf) 
		strcpy(channel, "unspecified"); 
	else {
        /* Fix for Knights Landing/Mill MIC */
		if (cputype == CPU_KNIGHTS_LANDING || cputype == CPU_KNIGHTS_MILL)
			sprintf(channel, "%u", (status & 0xf) + 3 * (bank == 15));
		else
			sprintf(channel, "%u", status & 0xf);
	}
	Wprintf("MEMORY CONTROLLER %s_CHANNEL%s_ERR\n", 
		mmm_mnemonic[(status >> 4) & 7],
		channel);
	Wprintf("Transaction: %s\n", mmm_desc[(status >> 4) & 7]);
}

void nehalem_decode_model(u64 status, u64 misc)
{
	u32 mca = status & 0xffff;
	if ((mca >> 11) == 1) { 	/* bus and interconnect QPI */
		decode_bitfield(status, qpi_status);
		if (status & MCI_STATUS_MISCV) {
			decode_numfield(misc, qpi_numbers);
			decode_bitfield(misc, qpi_misc);
		}
	} else if (mca == 0x0001) { /* internal unspecified */
		decode_bitfield(status, internal_error_status);
		decode_numfield(status, internal_error_numbers);
	} else if ((mca >> 7) == 1) { /* memory controller */
		decode_bitfield(status, nhm_memory_status);
		decode_numfield(status, nhm_memory_status_numbers);
		if (status & MCI_STATUS_MISCV)
			decode_numfield(misc, nhm_memory_misc_numbers);
	}
}

/* Only core errors supported. Same as Nehalem */
void xeon75xx_decode_model(struct mce *m, unsigned msize)
{
	u64 status = m->status;
	u32 mca = status & 0xffff;
	if (mca == 0x0001) { /* internal unspecified */
		decode_bitfield(status, internal_error_status);
		decode_numfield(status, internal_error_numbers);
	}
}

/* Nehalem-EP specific DIMM decoding */
void nehalem_memerr_misc(struct mce *m, int *channel, int *dimm)
{
	if (m->status & MCI_STATUS_MISCV) {
		*channel = EXTRACT(m->misc, 18, 19);
		*dimm = EXTRACT(m->misc, 16, 17);
	}
}

