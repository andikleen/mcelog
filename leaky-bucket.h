#ifndef LEAKY_BUCKET_H
#define LEAKY_BUCKET_H 1

#include <time.h>

/* Leaky bucket algorithm for triggers */

struct bucket_conf {
	unsigned capacity;
	unsigned agetime;
	unsigned char tunit;	/* 'd','h','m','s' */
	unsigned char log;
	char *trigger;
};

struct leaky_bucket {
	unsigned count;
	unsigned excess;
	time_t   tstamp;
};

int bucket_account(const struct bucket_conf *c, struct leaky_bucket *b,
		   unsigned inc);
int __bucket_account(const struct bucket_conf *c, struct leaky_bucket *b,
		   unsigned inc, time_t time);
char *bucket_output(const struct bucket_conf *c, struct leaky_bucket *b);
int bucket_conf_init(struct bucket_conf *c, const char *rate);
void bucket_init(struct leaky_bucket *b);
time_t bucket_time(void);

#endif
