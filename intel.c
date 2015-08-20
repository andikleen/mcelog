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
#include <stddef.h>
#include "mcelog.h"
#include "intel.h"
#include "bitfield.h"
#include "nehalem.h"
#include "memdb.h"
#include "page.h"
#include "sandy-bridge.h"
#include "ivy-bridge.h"
#include "haswell.h"
#include "xeon75xx.h"

int memory_error_support;

void intel_cpu_init(enum cputype cpu)
{
	if (cpu == CPU_NEHALEM || cpu == CPU_XEON75XX || cpu == CPU_INTEL ||
	    cpu == CPU_SANDY_BRIDGE || cpu == CPU_SANDY_BRIDGE_EP ||
	    cpu == CPU_IVY_BRIDGE || cpu == CPU_IVY_BRIDGE_EPEX ||
	    cpu == CPU_HASWELL || cpu == CPU_HASWELL_EPEX || cpu == CPU_BROADWELL ||
	    cpu == CPU_KNIGHTS_LANDING || cpu == CPU_SKYLAKE)
		memory_error_support = 1;
}

enum cputype select_intel_cputype(int family, int model)
{
	if (family == 15) { 
		if (model == 6) 
			return CPU_TULSA;
		return CPU_P4;
	} 
	if (family == 6) { 
		if (model >= 0x1a && model != 28) 
			memory_error_support = 1;

		if (model < 0xf) 
			return CPU_P6OLD;
		else if (model == 0xf || model == 0x17) /* Merom/Penryn */
			return CPU_CORE2;
		else if (model == 0x1d)
			return CPU_DUNNINGTON;
		else if (model == 0x1a || model == 0x2c || model == 0x1e ||
			 model == 0x25)
			return CPU_NEHALEM;
		else if (model == 0x2e || model == 0x2f)
			return CPU_XEON75XX;
		else if (model == 0x2a)
			return CPU_SANDY_BRIDGE;
		else if (model == 0x2d)
			return CPU_SANDY_BRIDGE_EP;
		else if (model == 0x3a)
			return CPU_IVY_BRIDGE;
		else if (model == 0x3e)
			return CPU_IVY_BRIDGE_EPEX;
		else if (model == 0x3c || model == 0x45 || model == 0x46)
			return CPU_HASWELL;
		else if (model == 0x3f)
			return CPU_HASWELL_EPEX;
		else if (model == 0x3d || model == 0x4f || model == 0x56)
			return CPU_BROADWELL;
		else if (model == 0x57)
			return CPU_KNIGHTS_LANDING;
		else if (model == 0x1c || model == 0x26 || model == 0x27 ||
			 model == 0x35 || model == 0x36 || model == 0x36 ||
			 model == 0x37 || model == 0x4a || model == 0x4c ||
			 model == 0x4d || model == 0x5a || model == 0x5d)
			return CPU_ATOM;
		else if (model == 0x4e || model == 0x5e)
			return CPU_SKYLAKE;
		if (model > 0x1a) {
			Eprintf("Family 6 Model %x CPU: only decoding architectural errors\n",
				model);
			return CPU_INTEL; 
		}
	}
	if (family > 6) { 
		Eprintf("Family %u Model %x CPU: only decoding architectural errors\n",
				family, model);
		return CPU_INTEL;
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
		unsigned corr_err_cnt = 0;
		int channel[2] = { (mca & 0xf) == 0xf ? -1 : (int)(mca & 0xf), -1 };
		int dimm[2] = { -1, -1 };

		switch (cputype) { 
		case CPU_NEHALEM:
			nehalem_memerr_misc(m, channel, dimm);
			break;
		case CPU_XEON75XX:
			xeon75xx_memory_error(m, recordlen, channel, dimm);
			break;
		case CPU_SANDY_BRIDGE_EP:
			sandy_bridge_ep_memerr_misc(m, channel, dimm);
			break;
		case CPU_IVY_BRIDGE_EPEX:
			ivy_bridge_ep_memerr_misc(m, channel, dimm);
			break;
		default:
			break;
		} 

		if (recordlen > offsetof(struct mce, mcgcap) && m->mcgcap & MCG_CMCI_P)
 			corr_err_cnt = EXTRACT(m->status, 38, 52);
		memory_error(m, channel[0], dimm[0], corr_err_cnt, recordlen);
		account_page_error(m, channel[0], dimm[0]);

		/* 
		 * When both DIMMs have a error account the error twice to the page.
		 */
		if (channel[1] != -1) {
			memory_error(m, channel[1], dimm[1], corr_err_cnt, recordlen);
			account_page_error(m, channel[1], dimm[1]);
		}

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
