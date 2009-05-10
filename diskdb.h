
#ifdef CONFIG_DISKDB
enum diskdb_options { 
	O_DATABASE = O_DISKDB,
	O_ERROR_TRIGGER,
	O_DUMP_MEMORY,
	O_RESET_MEMORY,
	O_DROP_OLD_MEMORY,	
};

void diskdb_resolve_addr(u64 addr);
int diskdb_modifier(int opt);
int diskdb_cmd(int opt, int ac, char **av);
void diskdb_usage(void);

#define DISKDB_OPTIONS \
	{ "database", 1, NULL, O_DATABASE },	\
	{ "error-trigger", 1, NULL, O_ERROR_TRIGGER },	\
	{ "dump-memory", 2, NULL, O_DUMP_MEMORY },	\
	{ "reset-memory", 2, NULL, O_RESET_MEMORY },	\
	{ "drop-old-memory", 0, NULL, O_DROP_OLD_MEMORY },

#else

static inline void diskdb_resolve_addr(u64 addr) {}
static inline int diskdb_modifier(int opt) { return 0; }
static inline int diskdb_cmd(int opt, int ac, char **av) { return 0; }
static inline void diskdb_usage(void) {}

#define DISKDB_OPTIONS

#endif
