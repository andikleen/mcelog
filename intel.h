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
	case CPU_BROADWELL_DE: \
	case CPU_BROADWELL_EPEX: \
	case CPU_ATOM:	\
	case CPU_KNIGHTS_LANDING: \
	case CPU_KNIGHTS_MILL: \
	case CPU_SKYLAKE: \
	case CPU_SKYLAKE_XEON: \
	case CPU_KABYLAKE: \
	case CPU_DENVERTON: \
	case CPU_ICELAKE_XEON: \
	case CPU_ICELAKE_DE: \
	case CPU_TREMONT_D: \
	case CPU_COMETLAKE: \
	case CPU_TIGERLAKE: \
	case CPU_ROCKETLAKE: \
	case CPU_ALDERLAKE: \
	case CPU_LAKEFIELD: \
	case CPU_SAPPHIRERAPIDS: \
	case CPU_RAPTORLAKE

