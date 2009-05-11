int decode_tsc_current(char **buf, int cpunum, enum cputype cputype, 
		       double mhz, __u64 tsc);
int decode_tsc_forced(char **buf, double mhz, __u64 tsc);


