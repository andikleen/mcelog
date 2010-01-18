/* Copyright (C) 2009/2010 Intel Corporation

   Decode Intel Xeon75xx memory errors. Requires the mce-75xx.ko driver
   load. The core errors are the same as Nehalem.

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

#include <stdio.h>
#include <stddef.h>
#include "mcelog.h"
#include "xeon75xx.h"

/* DIMM description */
struct aux_pfa_dimm {
	u8  fbd_channel_id;
	u8  ddr_channel_id;
	u8  ddr_dimm_id;
	u8  ddr_rank_id;
	u8  ddr_dimm_bank_id;
	u8  ddr_dimm_row_id;
	u8  ddr_dimm_column_id;
	u8  valid;
} __attribute__((packed));

enum {
	MCE_BANK_MBOX0		= 8,
	MCE_BANK_MBOX1		= 9,

	DIMM_VALID_FBD_CHAN      = (1 << 0),
	DIMM_VALID_DDR_CHAN      = (1 << 1),
	DIMM_VALID_DDR_DIMM      = (1 << 2),
	DIMM_VALID_DDR_RANK      = (1 << 3),
	DIMM_VALID_DIMM_BANK     = (1 << 4),
	DIMM_VALID_DIMM_ROW      = (1 << 5),
	DIMM_VALID_DIMM_COLUMN   = (1 << 6),
	DIMM_VALID_ALL		 = 0x7f,
};

static struct id {
	char *name;
	unsigned offset;
	unsigned valid;
	enum { 
		NL  = 1<<0,
		IND = 1<<1,
	} flags;
} ids[] = {
#define V(n,f,b) n, offsetof(struct aux_pfa_dimm, f), b
	{ V("FBD-Channel", fbd_channel_id,     DIMM_VALID_FBD_CHAN) },
	{ V("DDR-Channel", ddr_channel_id,     DIMM_VALID_DDR_CHAN) },
	{ V("DDR-DIMM",    ddr_dimm_id,        DIMM_VALID_DDR_DIMM) }, 
	{ V("DDR-Rank",    ddr_rank_id,        DIMM_VALID_DDR_RANK) },
	{ V("DIMM-Bank",   ddr_dimm_bank_id,   DIMM_VALID_DIMM_BANK), NL|IND },
	{ V("DIMM-Row",    ddr_dimm_row_id,    DIMM_VALID_DIMM_ROW) },
	{ V("DIMM-Column", ddr_dimm_column_id, DIMM_VALID_DIMM_COLUMN), NL },
#undef V
	{}
};

#if 0
/* Use for memdb channel output */

static int opt_number(char *buf, int val)
{
	if (val == (u8)-1) { 
		*buf++ = '?';
		return 1;
	}
	return sprintf(buf, "%u", val);
}

static void print_channel(int channel, char *buf)
{
	int n;
	n = opt_number(buf, ((channel) >> 8) & 0xff);
	buf[n++] = ':';
	opt_number(buf + n, channel & 0xff);
}
#endif

static void decode_dimm(struct aux_pfa_dimm *d, int *channel, int *dimm)
{
	if (d->valid == 0)
		return;
	if (d->valid & DIMM_VALID_DDR_DIMM) 
		*dimm = d->ddr_dimm_id;
	if (d->valid & (DIMM_VALID_DDR_CHAN|DIMM_VALID_FBD_CHAN)) {
		int fbd_chan = (d->valid & DIMM_VALID_FBD_CHAN) ? 
			d->fbd_channel_id : (u8)-1;
		int ddr_chan = (d->valid & DIMM_VALID_DDR_CHAN) ? 
			d->ddr_channel_id : (u8)-1;
		*channel = (fbd_chan << 8) | ddr_chan;
	}
}

static void print_dimm(int num, struct aux_pfa_dimm *d)
{
	struct id *id;
	int indent;
	int k;

	if (d->valid == 0)
		return;

	k = indent = Wprintf("DIMM %d: ", num);
	for (id = ids; id->name; id++) {
		if (d->valid & id->valid) 
			k += Wprintf("%s %u ", id->name, *((u8*)d + id->offset));
		if (k > 0) { 
			if (id->flags & NL) {
				Wprintf("\n");	
				k = 0;
			}
			if (id->flags & IND)
				Wprintf("%.*s", indent, "");
		}
	}
}

static int is_mem_err(struct mce *m, unsigned msize)
{
	if (msize < offsetof(struct mce, aux1) + sizeof(u64))
		return 0;
	if (m->bank != MCE_BANK_MBOX0 && m->bank != MCE_BANK_MBOX1)
		return 0;
	return 1;
}

union d {
	struct aux_pfa_dimm d;
	u64 val;
}; 

void 
xeon75xx_memory_error(struct mce *m, unsigned msize, int *channel, int *dimm)
{
	if (!is_mem_err(m, msize))
		return;

	decode_dimm(&((union d *)&m->aux0)->d, &channel[0], &dimm[0]);
	decode_dimm(&((union d *)&m->aux1)->d, &channel[1], &dimm[1]);
}


void xeon75xx_decode_dimm(struct mce *m, unsigned msize)
{
	if (!is_mem_err(m, msize))
		return;
	print_dimm(0, &((union d *)&m->aux0)->d);
	print_dimm(1, &((union d *)&m->aux1)->d);
}
