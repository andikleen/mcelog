#include "mcelog.h"
#include "intel.h"
#include "bitfield.h"
#include "nehalem.h"
#include "memdb.h"

int memory_error_support;

enum cputype select_intel_cputype(int family, int model)
{
	if (family == 15) { 
		if (model == 6) 
			return CPU_TULSA;
		return CPU_P4;
	} 
	if (family == 6) { 
		if (model < 0xf) 
			return CPU_P6OLD;
		else if (model == 0xf || model == 0x17) /* Merom/Penryn */
			return CPU_CORE2;
		else if (model == 0x1d)
			return CPU_DUNNINGTON;
		else if (model == 0x1a)
			return CPU_NEHALEM;

		if (model >= 0x1a) 
			memory_error_support = 1;
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

void intel_memory_error(struct mce *m)
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
		memory_error(m, channel, dimm, corr_err_cnt);
	}
}
