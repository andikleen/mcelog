enum cputype select_intel_cputype(int family, int model);
int is_intel_cpu(int cpu);
int mce_filter_intel(struct mce *m, unsigned recordlen);
void intel_cpu_init(enum cputype cpu);

extern int memory_error_support;
