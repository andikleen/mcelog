
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


