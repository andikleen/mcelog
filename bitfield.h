/* Generic bitfield decoder */

struct field {
	int start_bit;
	char **str;
	int stringlen;
};

struct numfield { 
	int start, end;
	char *name;
	char *fmt;
};

#define FIELD(start_bit, name) { start_bit, name, NELE(name) }
#define SBITFIELD(start_bit, string) { start_bit, ((char * [2]) { NULL, string }), 2 }

#define NUMBER(start, end, name) { start, end, name, "%Lu" }
#define HEXNUMBER(start, end, name) { start, end, name, "%Lx" }

void decode_bitfield(u64 status, struct field *fields);
void decode_numfield(u64 status, struct numfield *fields);

extern char *reserved_3bits[8];
extern char *reserved_1bit[2];
extern char *reserved_2bits[4];

