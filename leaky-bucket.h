#ifndef LEAKY_BUCKET_H
#define LEAKY_BUCKET_H 1

#include <time.h>
#include <stdbool.h>

/* Leaky bucket algorithm for triggers */

//bucket configuration
struct bucket_conf {
	unsigned capacity; //maximum amount of units the bucket can hold
	unsigned agetime; //time interval over which the bucket "drains" - how fast the bucket gets emptied over time
	unsigned char tunit;	/* 'd','h','m','s' */
	unsigned char log;
	char *trigger; //what action to trigger if bucket overflows
};

//the actual bucket
struct leaky_bucket {
	unsigned count; //current number of units in bucket
	unsigned excess;//how much bucket overflows (when it exceeds capacity)
	time_t   tstamp; //timestamp of last activity, used to track how much time has passed
};

int bucket_account(const struct bucket_conf *c, struct leaky_bucket *b, 
		   unsigned inc);
int __bucket_account(const struct bucket_conf *c, struct leaky_bucket *b, 
		   unsigned inc, time_t time);
char *bucket_output(const struct bucket_conf *c, struct leaky_bucket *b);
int bucket_conf_init(struct bucket_conf *c, const char *rate);
void bucket_init(struct leaky_bucket *b);
time_t bucket_time(void);
void bucket_age(const struct bucket_conf *c, struct leaky_bucket *b,
			time_t now);

#endif
