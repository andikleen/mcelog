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
	
	for (f = fields; f->str; f++) { 
		u64 v = (status >> f->start_bit) & bitmask(f->stringlen - 1);
		char *s = NULL;
		if (v < f->stringlen)
			s = f->str[v]; 
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
}
