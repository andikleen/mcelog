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
#include "memutil.h"
#include "leaky-bucket.h"

time_t __attribute__((weak)) bucket_time(void)
{
	return time(NULL);
}

void bucket_age(const struct bucket_conf *c, struct leaky_bucket *b,
			time_t now, unsigned char capacity_multiplier)
{
	long diff;
	diff = now - b->tstamp;
	if (diff >= c->agetime) { 
		unsigned age = (diff / (double)c->agetime) * c->capacity * capacity_multiplier;
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
		   unsigned inc, time_t t, unsigned char capacity_multiplier)
{
	if (c->capacity == 0)
		return 0;
	bucket_age(c, b, t, capacity_multiplier);
	b->count += inc; 
	if (b->count >= c->capacity * capacity_multiplier) {
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
	return __bucket_account(c, b, inc, bucket_time(), 1);
}

static int timeconv(char unit, int *out)
{
	unsigned corr = 1;
	switch (unit) { 
	case 'd': corr *= 24; /* FALL THROUGH */
	case 'h': corr *= 60; /* FALL THROUGH */
	case 'm': corr *= 60; /* FALL THROUGH */
	case 0:   break;
	default:
		*out = 1;
		return -1;
	}
	*out = corr;
	return 0;
}

/* Format leaky bucket as a string. Caller must free string */
char *bucket_output(const struct bucket_conf *c, struct leaky_bucket *b)
{
	char *buf;
	if (c->capacity == 0) {
		xasprintf(&buf, "not enabled");
	} else { 
		int unit = 0;
		timeconv(c->tunit, &unit);
		xasprintf(&buf, "%u in %u%c", b->count + b->excess,
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
	case 'g': cap *= 1000; /* FALL THROUGH */
	case 'm': cap *= 1000; /* FALL THROUGH */
	case 'k': cap *= 1000; /* FALL THROUGH */
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


#ifdef TEST_LEAKY_BUCKET
/* Stolen from the cpp documentation */
#define xstr(_s) str(_s)
#define str(_s) #_s

#define THRESHOLD_EVENTS_PER_PERIOD 100
#define EVENTS_PER_LOGGED_EVENT 10
#define SECONDS_PER_EVENT 86
/* Needs to be SECONDS_PER_EVENT * EVENTS_PER_LOGGED_EVENT * THRESHOLD_EVENTS_PER_PERIOD */
#define THRESHOLD_PERIOD 86000
#if THRESHOLD_PERIOD != (SECONDS_PER_EVENT * EVENTS_PER_LOGGED_EVENT * THRESHOLD_EVENTS_PER_PERIOD)
#  error THRESHOLD_PERIOD is Wrong!
#endif
#define RATE_STRING xstr(THRESHOLD_EVENTS_PER_PERIOD) " / " xstr(THRESHOLD_PERIOD)

#define EVENTS_PER_PERIOD_IN_TEST (THRESHOLD_EVENTS_PER_PERIOD * EVENTS_PER_LOGGED_EVENT)

#define PERIODS_TO_TEST 3
#define TOTAL_SECONDS_FOR_TEST (PERIODS_TO_TEST * THRESHOLD_PERIOD)
#define TOTAL_EVENTS (PERIODS_TO_TEST * EVENTS_PER_PERIOD_IN_TEST)

int main(int argc, char **argv)
{
	struct bucket_conf c;
	struct leaky_bucket b;
	time_t start_time;
	time_t event_time;
	int ret;
	int i;

#ifdef TEST_LEAKY_BUCKET_DEBUG
	printf("Testing with a rate of " RATE_STRING "\n");
#endif
	ret = bucket_conf_init(&c, RATE_STRING);
	if (ret)
		return ret;

	bucket_init(&b);
	start_time = b.tstamp;

	for (i = 1; i <= TOTAL_EVENTS; i++) {
		event_time = start_time + i * SECONDS_PER_EVENT;
		ret = __bucket_account(&c, &b, 1, event_time, 1);

#ifdef TEST_LEAKY_BUCKET_DEBUG
		if (ret)
			printf("Logging entry %d at %ld %ld\n", i, event_time - start_time, b.tstamp);
#else
		if (i < THRESHOLD_EVENTS_PER_PERIOD) {
			if (!ret){
				fprintf(stderr, "Did not log initial events - FAIL.\n");
				return -1;
			}
		} else {
			if (!(i % EVENTS_PER_LOGGED_EVENT) && !ret) {
				fprintf(stderr, "Did not log initial events - FAIL.\n");
				return -1;
			}
		}
#endif
	}

	return 0;
}
#endif
