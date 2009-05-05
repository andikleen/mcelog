#include "mcelog.h"
#include "intel.h"

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
	}
	Eprintf("Unknown Intel CPU type family %x model %x\n", family, model);
	return family == 6 ? CPU_P6OLD : CPU_GENERIC;
}
