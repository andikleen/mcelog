
struct map { 
	char *name;
	int value;
};

char *read_field(char *base, char *name);
unsigned read_field_num(char *base, char *name);
unsigned read_field_map(char *base, char *name, struct map *map);
