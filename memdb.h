#include <stdio.h>
#include "leaky-bucket.h"

struct err_type {
	struct leaky_bucket bucket;
	unsigned count;
};

enum printflags {
	DUMP_ALL  = (1 << 0),
	DUMP_BIOS = (1 << 1),
};	

void prefill_memdb(int do_dmi);
void memdb_config(void);
void dump_memory_errors(FILE *f, enum printflags flags);

void memory_error(struct mce *m, int channel, int dimm, unsigned corr_err_cnt,
			unsigned recordlen);

struct memdimm;
void memdb_trigger(char *msg, struct memdimm *md,  time_t t,
		   struct err_type *et, struct bucket_conf *bc, char *argv[], bool sync,
           const char* reporter);
struct memdimm *get_memdimm(int socketid, int channel, int dimm, int insert);
