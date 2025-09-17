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
#include "skylake_xeon.h"
#include "i10nm.h"
#include "sapphire.h"
#include "granite.h"
#include "diamond.h"

int memory_error_support;

void intel_cpu_init(enum cputype cpu)
{
	if (cpu == CPU_ATOM || cpu == CPU_CORE2 || cpu == CPU_DUNNINGTON ||
	    cpu == CPU_GENERIC || cpu == CPU_K8 || cpu == CPU_P4 ||
	    cpu == CPU_P6OLD || cpu == CPU_TULSA)
		memory_error_support = 0;
	else
		memory_error_support = 1;
}

enum cputype select_intel_cputype(int family, int model)
{
	int ret;

	if (family == 15) { 
		if (model == 6) 
			return CPU_TULSA;
		return CPU_P4;
	} 
	if (family == 6) { 
		if (model >= 0x1a && model != 28) 
			memory_error_support = 1;

		ret = lookup_intel_cputype(model);
		if (ret != -1)
			return ret;
		if (model > 0x1a) {
			Eprintf("Family 6 Model %u CPU: only decoding architectural errors\n",
				model);
			return CPU_INTEL; 
		}
	}
	if (family > 0xf) {
		ret = lookup_intel_cputype((family << 8) | model);
		if (ret != -1)
			return ret;
		Eprintf("Family %u Model %u CPU: only decoding architectural errors\n",
			family, model);
		return CPU_INTEL;
	}
	if (family > 6) { 
		Eprintf("Family %u Model %u CPU: only decoding architectural errors\n",
				family, model);
		return CPU_INTEL;
	}
	Eprintf("Unknown Intel CPU type family %u model %u\n", family, model);
	return family == 6 ? CPU_P6OLD : CPU_GENERIC;
}

int is_intel_cpu(int cpu)
{
	return cpu >= CPU_INTEL;
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
		case CPU_SANDY_BRIDGE_EP:
			sandy_bridge_ep_memerr_misc(m, channel, dimm);
			break;
		case CPU_IVY_BRIDGE_EPEX:
			ivy_bridge_ep_memerr_misc(m, channel, dimm);
			break;
		case CPU_HASWELL_EPEX:
		case CPU_BROADWELL_EPEX:
			haswell_memerr_misc(m, channel, dimm);
			break;
		case CPU_SKYLAKE_XEON:
			skylake_memerr_misc(m, channel, dimm);
			break;
		case CPU_ICELAKE_XEON:
		case CPU_ICELAKE_DE:
		case CPU_TREMONT_D:
			i10nm_memerr_misc(m, channel, dimm);
			break;
		case CPU_SAPPHIRERAPIDS:
		case CPU_EMERALDRAPIDS:
			sapphire_memerr_misc(m, channel, dimm);
			break;
		case CPU_GRANITERAPIDS:
		case CPU_SIERRAFOREST:
		case CPU_CLEARWATERFOREST:
			granite_memerr_misc(m, channel, dimm);
			break;
		case CPU_DIAMONDRAPIDS:
			diamond_memerr_misc(m, channel, dimm);
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
