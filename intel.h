enum cputype select_intel_cputype(int family, int model);
int is_intel_cpu(int cpu);
int mce_filter_intel(struct mce *m, unsigned recordlen);
void intel_cpu_init(enum cputype cpu);

extern int memory_error_support;

#define CASE_INTEL_CPUS \
	case CPU_P6OLD: \
	case CPU_CORE2: \
	case CPU_NEHALEM: \
	case CPU_DUNNINGTON: \
	case CPU_TULSA:	\
	case CPU_P4: \
	case CPU_INTEL: \
	case CPU_XEON75XX: \
	case CPU_SANDY_BRIDGE_EP: \
	case CPU_SANDY_BRIDGE: \
	case CPU_IVY_BRIDGE: \
	case CPU_IVY_BRIDGE_EPEX: \
	case CPU_HASWELL: \
	case CPU_HASWELL_EPEX: \
	case CPU_BROADWELL: \
	case CPU_KNIGHTS_LANDING: \
	case CPU_SKYLAKE

