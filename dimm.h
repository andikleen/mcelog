void close_dimm_db(void);
int open_dimm_db(char *fn);
void new_error(unsigned long long addr, unsigned long max_error, char *trigger);
void reset_dimm(char *locator);
void gc_dimms(void);
void dump_all_dimms(void);
void dump_dimm(char *locator);

