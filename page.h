#include <stdio.h>
#include <time.h>

struct memdimm;
void account_page_error(struct mce *m, int channel, int dimm);
void dump_page_errors(FILE *);
void page_setup(void);


