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
	char buf[60];
	int len;
	
	for (f = fields; f->str; f++) { 
		u64 v = (status >> f->start_bit) & bitmask(f->stringlen - 1);
		char *s = NULL;
		if (v < f->stringlen)
			s = f->str[v]; 
		if (!s) { 
			if (v == 0) 
				continue;
			s = buf; 
			buf[(sizeof buf)-1] = 0;
			snprintf(buf, (sizeof buf) - 1, "<%u:%llx>", f->start_bit, v);
		}
		len = strlen(s);
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

void decode_numfield(u64 status, struct numfield *fields)
{
	struct numfield *f;
	for (f = fields; f->name; f++) {
		u64 mask = (1ULL << (f->end - f->start + 1)) - 1;
		u64 v = (status >> f->start) & mask;
		if (v > 0 || f->force) { 
			char fmt[30];
			snprintf(fmt, 30, "%%s: %s\n", f->fmt ? f->fmt : "%llu");
			Wprintf(fmt, f->name, v);
		}
	}
}
