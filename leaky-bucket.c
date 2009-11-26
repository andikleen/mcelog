/* Copyright (C) 2009 Intel Corporation 
   Author: Andi Kleen
   Leaky bucket algorithm. This is used for all error triggers.

   mcelog is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version
   2.

   mcelog is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should find a copy of v2 of the GNU General Public License somewhere
   on your Linux system; if not, write to the Free Software Foundation, 
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <ctype.h>
#include "leaky-bucket.h"

time_t __attribute__((weak)) bucket_time(void)
{
	return time(NULL);
}

static void bucket_age(const struct bucket_conf *c, struct leaky_bucket *b,
			time_t now)
{
	long diff;
	diff = now - b->tstamp;
	if (diff >= c->agetime) { 
		unsigned age = (diff / (double)c->agetime) * c->capacity;
		b->tstamp = now;
		if (age > b->count)
			b->count = 0;
		else
			b->count -= age;
		b->excess = 0;
	}
}

/* Account increase in leaky bucket. Return 1 if bucket overflowed. */
int __bucket_account(const struct bucket_conf *c, struct leaky_bucket *b, 
		   unsigned inc, time_t t)
{
	if (c->capacity == 0)
		return 0;
	bucket_age(c, b, t);
	b->count += inc; 
	if (b->count >= c->capacity) {
		b->excess += b->count;
		/* should disable overflow completely in the same time unit */
		b->count = 0;
		return 1;
	}
	return 0;
}

int bucket_account(const struct bucket_conf *c, struct leaky_bucket *b, 
		   unsigned inc) 
{
	return __bucket_account(c, b, inc, bucket_time());
}

static int timeconv(char unit, int *out)
{
	unsigned corr = 1;
	switch (unit) { 
	case 'd': corr *= 24;
	case 'h': corr *= 3600;
	case 'm': corr *= 60;
	case 0:   break;
	default: return -1;
	}
	*out = corr;
	return 0;
}

/* Format leaky bucket as a string. Caller must free string */
char *bucket_output(const struct bucket_conf *c, struct leaky_bucket *b)
{
	char *buf;
	if (c->capacity == 0) {
		asprintf(&buf, "not enabled");
	} else { 
		int unit = 0;
		//bucket_age(c, b, bucket_time());
		timeconv(c->tunit, &unit);
		asprintf(&buf, "%u in %u%c", b->count + b->excess, 
			c->agetime/unit, c->tunit);
	}
	return buf;
}

/* Parse user specified capacity / rate string */
/* capacity / time 
   time: number [hmds]
   capacity: number [kmg] */
static int parse_rate(const char *rate, struct bucket_conf *c)
{
	char cunit[2], tunit[2];
	unsigned cap, t;
	int n;
	int unit;

	cunit[0] = 0;
	tunit[0] = 0;
	n = sscanf(rate, "%u %1s / %u %1s", &cap, cunit, &t, tunit);
	if (n != 4)  { 
		cunit[0] = 0;
		tunit[0] = 0;
		if (n <= 2) {
			n = sscanf(rate, "%u / %u %1s", &cap, &t, tunit);
			if (n < 2)
				return -1;
		} else
			return -1;
	}
	if (t == 0 || cap == 0)
		return -1;
	switch (tolower(cunit[0])) { 
	case 'g': cap *= 1000;
	case 'm': cap *= 1000;
	case 'k': cap *= 1000;
	case 0:   break;
	default:  return -1;
	}
	c->tunit = tolower(tunit[0]);
	if (timeconv(c->tunit, &unit) < 0)
		return -1;
	c->agetime = unit * t;
	c->capacity = cap;
	return 0;
}

/* Initialize leaky bucket conf for given user rate/capacity string. <0 on error */
int bucket_conf_init(struct bucket_conf *c, const char *rate)
{
	if (parse_rate(rate, c) < 0)
		return -1;
	c->trigger = NULL;
	return 0;
}

/* Initialize leaky bucket instance. */
void bucket_init(struct leaky_bucket *b)
{
	b->count = 0;
	b->excess = 0;
	b->tstamp = bucket_time();
}
