#include <string.h>
#include <stdio.h>
#include "mcelog.h"
#include "bitfield.h"

char *reserved_3bits[8];
char *reserved_1bit[2];
char *reserved_2bits[4];

static u64 bitmask(u64 i)
{
	u64 mask = 1;
	while (mask < i) 
		mask = (mask << 1) | 1; 
	return mask;
}

void decode_bitfield(u64 status, struct field *fields)
{
	struct field *f;
	int linelen = 0;
	char *delim = "";
	u64 parsed = 0;

	for (f = fields; f->str; f++) { 
		u64 mask = bitmask(f->stringlen - 1);
		u64 v = (status >> f->start_bit) & mask;
		char *s = NULL;
		if (v < f->stringlen)
			s = f->str[v]; 		
		parsed |= (mask << f->start_bit);
		if (!s) { 
			if (v == 0) 
				continue;
			char buf[60];
			s = buf; 
			snprintf(buf, sizeof buf, "<%u:%Lx>", f->start_bit, v);
		}
		int len = strlen(s);
		if (linelen + len > 75) {
			delim = "\n";
			linelen = 0;
		}
		Wprintf("%s%s", delim, s);
		delim = " ";
		linelen += len + 1; 
	}
	if (linelen > 0) 
		Wprintf("\n");

	if (status & ~parsed) { 
		int k;
		for (k = 0; k < 64; k++) { 
			if ((status & ~parsed) & (1ULL << k))
				Wprintf("Unknown bit %u set\n", k);
		}
	} 
}
