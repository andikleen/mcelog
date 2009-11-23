/* Generic bitfield decoder */

struct field {
	unsigned start_bit;
	char **str;
	unsigned stringlen;
};

struct numfield { 
	unsigned start, end;
	char *name;
	char *fmt;
	int force;
};

#define FIELD(start_bit, name) { start_bit, name, NELE(name) }
#define SBITFIELD(start_bit, string) { start_bit, ((char * [2]) { NULL, string }), 2 }

#define NUMBER(start, end, name) { start, end, name, "%Lu", 0 }
#define NUMBERFORCE(start, end, name) { start, end, name, "%Lu", 1 }
#define HEXNUMBER(start, end, name) { start, end, name, "%Lx", 0 }
#define HEXNUMBERFORCE(start, end, name) { start, end, name, "%Lx", 1 }

void decode_bitfield(u64 status, struct field *fields);
void decode_numfield(u64 status, struct numfield *fields);

extern char *reserved_3bits[8];
extern char *reserved_1bit[2];
extern char *reserved_2bits[4];

#define MASK(x) ((1ULL << (1 + (x))) - 1)
#define EXTRACT(v, a, b) (((v) >> (a)) & MASK((b)-(a)))
