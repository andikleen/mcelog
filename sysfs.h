
struct map { 
	char *name;
	int value;
};

char *read_field(char *base, char *name);
unsigned read_field_num(char *base, char *name);
unsigned read_field_map(char *base, char *name, struct map *map);

int sysfs_write(const char *name, const char *format, ...)
	__attribute__((format(printf,2,3)));
int sysfs_available(const char *name, int flags);
