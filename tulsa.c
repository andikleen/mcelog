/* Copyright (C) 2009 Intel Corporation
   Decode Intel Xeon MP 7100 series (Tulsa) specific machine check errors.

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
#include "bitfield.h"
#include "tulsa.h"

/* See IA32 SDM Vol3B Appendix E.4.1 ff */

static struct numfield corr_numbers[] = { 
	NUMBER(32, 39, "Corrected events"),
	{}
};

static struct numfield ecc_numbers[] = { 
	HEXNUMBER(44, 51, "ECC syndrome"),	
	{},
};

void tulsa_decode_model(u64 status, u64 misc)
{
	decode_numfield(status, corr_numbers);
	if (status & (1ULL << 52))
		decode_numfield(status, ecc_numbers);
	/* MISC register not documented in the SDM. Let's just dump hex for now. */
	if (status &  MCI_STATUS_MISCV)
		Wprintf("MISC format %Lx value %Lx\n", (status >> 40) & 3, misc);
}
