/* Copyright (C) 2009 Intel Corporation 
   Author: Andi Kleen
   Simple in memory error database for mcelog running in daemon mode

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
#include <stddef.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "mcelog.h"
#include "memutil.h"
#include "config.h"
#include "dmi.h"
#include "memdb.h"
#include "leaky-bucket.h"
#include "trigger.h"
#include "intel.h"
#include "page.h"

struct memdimm {
	struct memdimm *next;
	int channel;			/* -1: unknown */
	int dimm;			/* -1: unknown */
	int socketid;
	struct err_type ce;
	struct err_type uc;
	char *name;
	char *location;
	struct dmi_memdev *memdev;
};

struct err_triggers {
	struct bucket_conf ce_bucket_conf;
	struct bucket_conf uc_bucket_conf;
	char *type;
};

#define SHASH 17

static int md_numdimms;
static struct memdimm *md_dimms[SHASH];

static struct err_triggers dimms = { .type = "DIMM" };
static struct err_triggers sockets = { .type = "Socket" };

static int memdb_enabled;
static int sockdb_enabled;

#define FNV32_OFFSET 2166136261U
#define FNV32_PRIME 0x01000193
#define O(x) ((x) & 0xff)

/* FNV 1a 32bit, max 16k sockets, 8bit dimm/channel */
static unsigned dimmhash(unsigned socket, int dimm, unsigned ch)
{
        unsigned hash = FNV32_OFFSET;
	hash = (hash ^ O(socket)) * FNV32_PRIME;
	hash = (hash ^ O(socket >> 8)) * FNV32_PRIME;
	hash = (hash ^ O(dimm)) * FNV32_PRIME;
	hash = (hash ^ O(ch)) * FNV32_PRIME;
        return hash % SHASH;
}

/* Search DIMM in hash table */
struct memdimm *get_memdimm(int socketid, int channel, int dimm, int insert)
{
	struct memdimm *md;
	unsigned h;

	h = dimmhash(socketid, dimm, channel);
	for (md = md_dimms[h]; md; md = md->next) { 
		if (md->socketid == socketid && 
			md->channel == channel && 
			md->dimm == dimm)
			break;	
	}
	if (md || !insert)
		return md;

	md = xalloc(sizeof(struct memdimm));
	md->next = md_dimms[h];
	md_dimms[h] = md;
	md->socketid = socketid;
	md->channel = channel;
	md->dimm = dimm;
	md_numdimms++;
	bucket_init(&md->ce.bucket);
	bucket_init(&md->uc.bucket);
	return md;
}

enum {
	NUMLEN  = 30,
	MAX_ENV = 20,
};

static char *number(char *buf, long num)
{
	snprintf(buf, NUMLEN, "%ld", num);
	return buf;
}

static char *format_location(struct memdimm *md)
{
	char numbuf[NUMLEN], numbuf2[NUMLEN];
	char *location;

	xasprintf(&location, "SOCKET:%d CHANNEL:%s DIMM:%s [%s%s%s]",
		md->socketid, 
		md->channel == -1 ? "?" : number(numbuf, md->channel),
		md->dimm == -1 ? "?" : number(numbuf2, md->dimm),
		md->location ? md->location : "",
		md->location && md->name ? " " : "",
		md->name ? md->name : ""); 
	return location;
}

/* Run a user defined trigger when a error threshold is crossed. */
void memdb_trigger(char *msg, struct memdimm *md,  time_t t,
		struct err_type *et, struct bucket_conf *bc, char *args[], bool sync,
		const char* reporter)
{
	struct leaky_bucket *bucket = &et->bucket;
	char *env[MAX_ENV]; 
	int ei = 0;
	int i;
	char *location = format_location(md);
	char *thresh = bucket_output(bc, bucket);
	char *out;

	xasprintf(&out, "%s: %s", msg, thresh);
	if (bc->log) { 
		Gprintf("%s\n", out); 
		Gprintf("Location %s\n", location);
	}
	if (bc->trigger == NULL)
		goto out;
	xasprintf(&env[ei++], "PATH=%s", getenv("PATH") ?: "/sbin:/usr/sbin:/bin:/usr/bin");
	xasprintf(&env[ei++], "THRESHOLD=%s", thresh);
	xasprintf(&env[ei++], "TOTALCOUNT=%lu", et->count);
	xasprintf(&env[ei++], "LOCATION=%s", location);
	if (md->location)
		xasprintf(&env[ei++], "DMI_LOCATION=%s", md->location);
	if (md->name)
		xasprintf(&env[ei++], "DMI_NAME=%s", md->name);
	if (md->dimm != -1)
		xasprintf(&env[ei++], "DIMM=%d", md->dimm);
	if (md->channel != -1)
		xasprintf(&env[ei++], "CHANNEL=%d", md->channel);
	xasprintf(&env[ei++], "SOCKETID=%d", md->socketid);
	xasprintf(&env[ei++], "CECOUNT=%lu", md->ce.count);
	xasprintf(&env[ei++], "UCCOUNT=%lu", md->uc.count);
	if (t)
		xasprintf(&env[ei++], "LASTEVENT=%lu", t);
	xasprintf(&env[ei++], "AGETIME=%u", bc->agetime);
	// XXX human readable version of agetime
	xasprintf(&env[ei++], "MESSAGE=%s", out);
	xasprintf(&env[ei++], "THRESHOLD_COUNT=%d", bucket->count);
	env[ei] = NULL;	
	assert(ei < MAX_ENV);
	run_trigger(bc->trigger, args, env, sync, reporter);
	for (i = 0; i < ei; i++) {
		free(env[i]);
		env[i] = NULL;
	}
out:
	free(location);
	location = NULL;
	free(out);
	out = NULL;
	free(thresh);
	thresh = NULL;
}

/* 
 * Lost some errors. Assume they were CE. Only works for the sockets because
 * we have no clues where they are.
 */
static void
account_over(struct err_triggers *t, struct memdimm *md, struct mce *m, unsigned corr_err_cnt, const char* reporter)
{
	if (corr_err_cnt && --corr_err_cnt > 0) {
		md->ce.count += corr_err_cnt;
		if (__bucket_account(&t->ce_bucket_conf, &md->ce.bucket, corr_err_cnt, m->time)) { 
			char *msg;
			xasprintf(&msg, "Fallback %s memory error count %d exceeded threshold",
				 t->type, corr_err_cnt);
			memdb_trigger(msg, md, 0, &md->ce, &t->ce_bucket_conf, NULL, false, reporter);
			free(msg);
			msg = NULL;
		}
	}
}

static void 
account_memdb(struct err_triggers *t, struct memdimm *md, struct mce *m, const char* reporter)
{
	char *msg;

	xasprintf(&msg, "%scorrected %s memory error count exceeded threshold",
		(m->status & MCI_STATUS_UC) ? "Un" : "", t->type);

	if (m->status & MCI_STATUS_UC) { 
		md->uc.count++;
		if (__bucket_account(&t->uc_bucket_conf, &md->uc.bucket, 1, m->time))
			memdb_trigger(msg, md, m->time, &md->uc, &t->uc_bucket_conf, NULL, false, reporter);
	} else {
		md->ce.count++;
		if (__bucket_account(&t->ce_bucket_conf, &md->ce.bucket, 1, m->time))
			memdb_trigger(msg, md, m->time, &md->ce, &t->ce_bucket_conf, NULL, false, reporter);
	}
	free(msg);
	msg = NULL;
}

/* 
 * A memory error happened, record it in the memdb database and run
 * triggers if needed.
 * ch/dimm == -1: Unspecified DIMM on the channel
 */
void memory_error(struct mce *m, int ch, int dimm, unsigned corr_err_cnt, 
		unsigned recordlen)
{
	struct memdimm *md;

	if (recordlen < offsetof(struct mce, socketid)) { 
		static int warned;
		if (!warned) {
			Eprintf("Cannot account memory errors because kernel does not report socketid");
			warned = 1;
		}
		return;
	}

	if (memdb_enabled && (ch != -1 || dimm != -1)) {
		md = get_memdimm(m->socketid, ch, dimm, 1);
		account_memdb(&dimms, md, m, "memdb");
	}

	if (sockdb_enabled) {
		md = get_memdimm(m->socketid, -1, -1, 1);
		account_over(&sockets, md, m, corr_err_cnt, "sockdb_fallback");
		account_memdb(&sockets, md, m, "sockdb_memdb");
	}
}

/* Compare two dimms for sorting. */
static int cmp_dimm(const void *a, const void *b)
{
	const struct memdimm *ma = *(void **)a;
	const struct memdimm *mb = *(void **)b;
	if (ma->socketid != mb->socketid)
		return ma->socketid - mb->socketid;
	if (ma->channel != mb->channel)
		return ma->channel - mb->channel;
	return ma->dimm - mb->dimm;
}

/* Dump CE or UC errors */
static void dump_errtype(char *name, struct err_type *e, FILE *f, enum printflags flags,
			 struct bucket_conf *bc)
{
	int all = (flags & DUMP_ALL);
	char *s;

	bucket_age(bc, &e->bucket, bucket_time());
	if (e->count || e->bucket.count || all)
		fprintf(f, "%s:\n", name);
	if (e->count || all) {
		fprintf(f, "\t%u total\n", e->count);
	}
	if (bc->capacity && (e->bucket.count || all)) {
		s = bucket_output(bc, &e->bucket);
		fprintf(f, "\t%s\n", s);  
		free(s);
		s = NULL;
	}
}

static void dump_bios(struct memdimm *md, FILE *f)
{
	int n = 0;

	if (md->name)
		n += fprintf(f, "DMI_NAME \"%s\"", md->name);
	if (md->location) { 
		if (n > 0)
			fputc(' ', f);
		n += fprintf(f, "DMI_LOCATION \"%s\"", md->location);
	}
	if (n > 0)
		fputc('\n', f);
}

static void dump_dimm(struct memdimm *md, FILE *f, enum printflags flags)
{
	if (md->ce.count + md->uc.count > 0 || (flags & DUMP_ALL)) {
		fprintf(f, "SOCKET %u", md->socketid);
		if (md->channel == -1)
			fprintf(f, " CHANNEL any");
		else
			fprintf(f, " CHANNEL %d", md->channel);
		if (md->dimm == -1) 
			fprintf(f, " DIMM any");
		else
			fprintf(f, " DIMM %d", md->dimm);
		fputc('\n', f);

		if (flags & DUMP_BIOS)
			dump_bios(md, f);
		dump_errtype("corrected memory errors", &md->ce, f, flags, 
				&dimms.ce_bucket_conf);
		dump_errtype("uncorrected memory errors", &md->uc, f, flags, 
				&dimms.uc_bucket_conf);
	}
}

/* Sort and dump DIMMs */
void dump_memory_errors(FILE *f, enum printflags flags)
{
	int i, k;
	struct memdimm *md, **da;

	da = xalloc(sizeof(void *) * md_numdimms);
	k = 0;
	for (i = 0; i < SHASH; i++) {
		for (md = md_dimms[i]; md; md = md->next)
			da[k++] = md;
	}
	qsort(da, md_numdimms, sizeof(void *), cmp_dimm);
	for (i = 0; i < md_numdimms; i++)  {
		if (i > 0)  
			fputc('\n', f);
		else
			fprintf(f, "Memory errors\n");
		dump_dimm(da[i], f, flags);
	}
	free(da);
	da = NULL;
}

void memdb_config(void)
{
	int n;

	n = config_bool("dimm", "dimm-tracking-enabled");
	if (n < 0) 
		memdb_enabled = memory_error_support;
	else
		memdb_enabled = n; 

	config_trigger("dimm", "ce-error", &dimms.ce_bucket_conf);
	config_trigger("dimm", "uc-error", &dimms.uc_bucket_conf);

	n = config_bool("socket", "socket-tracking-enabled");
	if (n < 0) 
		sockdb_enabled = memory_error_support;
	else
		sockdb_enabled = n; 

	config_trigger("socket", "mem-ce-error", &sockets.ce_bucket_conf);
	config_trigger("socket", "mem-uc-error", &sockets.uc_bucket_conf);
}

static int 
parse_dimm_addr(char *bl, unsigned *socketid, unsigned *channel, unsigned *dimm)
{
	if (!bl)
		return 0;
	if (sscanf(bl + strcspn(bl, "_"), "_Node%u_Channel%u_Dimm%u", socketid, 
		   channel, dimm) == 3)
		return 1;
	if (sscanf(bl, "NODE %u CHANNEL %u DIMM %u", socketid,
		   channel, dimm) == 3)
		return 1;
	/* Add more DMI formats here */
	/* For new AMI BIOS Node0_Bank0 */
	if (sscanf(bl, "Node%u_Bank%u", socketid, dimm) == 2)
		return 1;

	/* For old AMI BIOS A1_BANK0*/
	if (sscanf(bl, "A%u_BANK%u", socketid, dimm) == 2)
		return 1;

	return 0;		
}

/* Prepopulate DIMM database from BIOS information */
void prefill_memdb(int do_dmi)
{
	static int initialized;
	int i;
	int missed = 0;
	unsigned socketid, channel, dimm;

	if (initialized)
		return;
	memdb_config();
	if (!memdb_enabled)
		return;
	initialized = 1;
	if (config_bool("dimm", "dmi-prepopulate") == 0 || !do_dmi)
		return;
	if (opendmi() < 0)
		return;

	for (i = 0; dmi_dimms[i]; i++) {
		struct memdimm *md;
		struct dmi_memdev *d = dmi_dimms[i];
		char *bl;

		bl = dmi_getstring(&d->header, d->bank_locator);
		if (!parse_dimm_addr(bl, &socketid, &channel, &dimm)) {
			missed++;
			continue;
		}

		md = get_memdimm(socketid, channel, dimm, 1);
		if (md->memdev) { 
			/* dups -- likely parse error */
			missed++;
			continue;
		}
		md->memdev = d;
		md->location = xstrdup(bl);
		md->name = xstrdup(dmi_getstring(&d->header, d->device_locator));
	}
	if (missed) { 
		static int warned;
		if (!warned) {
			Eprintf("failed to prefill DIMM database from DMI data");
			warned = 1;
		}
	}
}
