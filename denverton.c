/* Copyright (C) 2016 Intel Corporation
   Decode Intel Denverton specific machine check errors.

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
#include "denverton.h"
#include "memdb.h"

/* See IA32 SDM Vol3B Table 16-33 */

static struct field mc_bits[] = {
	SBITFIELD(16, "Cmd/Addr parity"),
	SBITFIELD(17, "Corrected Demand/Patrol Scrub Error"),
	SBITFIELD(18, "Uncorrected patrol scrub error"),
	SBITFIELD(19, "Uncorrected demand read error"),
	SBITFIELD(20, "WDB read ECC"),
	{}
};

void denverton_decode_model(int cputype, int bank, u64 status, u64 misc)
{
	switch (bank) {
	case 6: case 7:
		Wprintf("MemCtrl: ");
		decode_bitfield(status, mc_bits);
		break;
	}
}
