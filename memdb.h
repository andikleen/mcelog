#include <stdio.h>

enum printflags {
	DUMP_ALL,
	DUMP_BIOS,
};	

void prefill_memdb(void);
void memdb_config(void);
void dump_memory_errors(FILE *f, enum printflags flags);

void memory_error(struct mce *m, int channel, int dimm, unsigned corr_err_cnt);


