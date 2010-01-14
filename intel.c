/* Copyright (C) 2009 Intel Corporation 
   Author: Andi Kleen
   Common Intel CPU code.

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
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
#include "mcelog.h"
#include "intel.h"
#include "bitfield.h"
#include "nehalem.h"
#include "memdb.h"
#include "page.h"

int memory_error_support;

enum cputype select_intel_cputype(int family, int model)
{
	if (family == 15) { 
		if (model == 6) 
			return CPU_TULSA;
		return CPU_P4;
	} 
	if (family == 6) { 
		if (model >= 0x1a) 
			memory_error_support = 1;

		if (model < 0xf) 
			return CPU_P6OLD;
		else if (model == 0xf || model == 0x17) /* Merom/Penryn */
			return CPU_CORE2;
		else if (model == 0x1d)
			return CPU_DUNNINGTON;
		else if (model == 0x1a)
			return CPU_NEHALEM;
	}
	Eprintf("Unknown Intel CPU type family %x model %x\n", family, model);
	return family == 6 ? CPU_P6OLD : CPU_GENERIC;
}

int is_intel_cpu(int cpu)
{
	switch (cpu) {
	CASE_INTEL_CPUS:
		return 1;
	} 
	return 0;
}

static int intel_memory_error(struct mce *m, unsigned recordlen)
{
	u32 mca = m->status & 0xffff;
	if ((mca >> 7) == 1) { 
		int cmci = 0;
		unsigned corr_err_cnt = 0;
		int channel = (mca & 0xf) == 0xf ? -1 : (int)(mca & 0xf);
		int dimm = -1;

		switch (cputype) { 
		case CPU_NEHALEM:
			nehalem_memerr_misc(m, &channel, &dimm);
			break;
		default:
			cmci = !!(m->mcgcap & MCG_CMCI_P);
			break;
		} 

		if (cmci)
 			corr_err_cnt = EXTRACT(m->status, 38, 52);
		memory_error(m, channel, dimm, corr_err_cnt, recordlen);

		account_page_error(m, channel, dimm, corr_err_cnt);

		return 1;
	}

	return 0;
}

/* No bugs known, but filter out memory errors if the user asked for it */
int mce_filter_intel(struct mce *m, unsigned recordlen)
{
	if (intel_memory_error(m, recordlen) == 1) 
		return !filter_memory_errors;
	return 1;
}
