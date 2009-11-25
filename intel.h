enum cputype select_intel_cputype(int family, int model);
int is_intel_cpu(int cpu);
void intel_memory_error(struct mce *m);

extern int memory_error_support;


#define CASE_INTEL_CPUS \
	case CPU_P6OLD: \
	case CPU_CORE2: \
	case CPU_NEHALEM: \
	case CPU_DUNNINGTON: \
	case CPU_TULSA:	\
	case CPU_P4

