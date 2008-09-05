
#define SINGLEBIT(n,d) static char *n[2] = { [1] = d }; 

struct field {
	int start_bit;
	char **str;
	int stringlen;
};

#define FIELD(start_bit, name) { start_bit, name, NELE(name) }

void decode_bitfield(u64 status, struct field *fields);

extern char *reserved_3bits[8];
extern char *reserved_1bit[2];
extern char *reserved_2bits[4];

