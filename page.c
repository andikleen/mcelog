/* Copyright (C) 2009 Intel Corporation 
   Author: Andi Kleen
   Memory error accounting per page

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

/* NB 
   investigate other data structures. Primary consideration would 
   be space efficiency. rbtree nodes are rather large. 

   Do we need aging? Right now the only way to get rid of old nodes
   is to restart. */
#define _GNU_SOURCE 1 
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <assert.h>
#include "memutil.h"
#include "trigger.h"
#include "mcelog.h"
#include "rbtree.h"
#include "list.h"
#include "leaky-bucket.h"
#include "page.h"
#include "config.h"
#include "memdb.h"
#include "sysfs.h"

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)

enum { PAGE_ONLINE = 0, PAGE_OFFLINE = 1, PAGE_OFFLINE_FAILED = 2 };

struct mempage { 
	struct rb_node nd;
	/* one char used by rb_node */
	char offlined;
	char triggered;
	// 1(32bit)-5(64bit) bytes of padding to play with here
	u64 addr;
	struct err_type ce;
};

#define N ((PAGE_SIZE - sizeof(struct list_head)) / sizeof(struct mempage))
#define to_cluster(mp)	(struct mempage_cluster *)((long)(mp) & ~((long)(PAGE_SIZE - 1)))

struct mempage_cluster {
	struct list_head lru;
	struct mempage mp[N];
	int mp_used;
};

struct mempage_replacement {
	struct leaky_bucket bucket;
	unsigned count;
};

enum {
	MAX_ENV = 20,
};

static int corr_err_counters;
static struct mempage_cluster *mp_cluster;
static struct mempage_replacement mp_repalcement;
static struct rb_root mempage_root;
static LIST_HEAD(mempage_cluster_lru_list);
static struct bucket_conf page_trigger_conf;
static struct bucket_conf mp_replacement_trigger_conf;
static char *page_error_pre_soft_trigger, *page_error_post_soft_trigger;

static const char *page_state[] = {
	[PAGE_ONLINE] = "online",
	[PAGE_OFFLINE] = "offline",
	[PAGE_OFFLINE_FAILED] = "offline-failed",
};

static struct mempage *mempage_alloc(void)
{
	if (!mp_cluster || mp_cluster->mp_used == N) {
		mp_cluster = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mp_cluster == MAP_FAILED)
			Enomem();
	}

	return &mp_cluster->mp[mp_cluster->mp_used++];
}

static struct mempage *mempage_replace(void)
{
	/* If no free mp_cluster, reuse the last mp_cluster of the LRU list  */
	if (mp_cluster->mp_used == N) {
		mp_cluster = list_last_entry(&mempage_cluster_lru_list, struct mempage_cluster, lru);
		mp_cluster->mp_used = 0;
	}

	return &mp_cluster->mp[mp_cluster->mp_used++];
}

static struct mempage *mempage_lookup(u64 addr)
{
	struct rb_node *n = mempage_root.rb_node;

	while (n) {
		struct mempage *mp = rb_entry(n, struct mempage, nd);

		if (addr < mp->addr)
			n = n->rb_left;
		else if (addr > mp->addr)
			n = n->rb_right;
		else
			return mp;
	}
	return NULL;
}

static struct mempage *
mempage_insert_lookup(u64 addr, struct rb_node * node)
{
	struct rb_node **p = &mempage_root.rb_node;
	struct rb_node *parent = NULL;
	struct mempage *mp;

	while (*p) {
		parent = *p;
		mp = rb_entry(parent, struct mempage, nd);

		if (addr < mp->addr)
			p = &(*p)->rb_left;
		else if (addr > mp->addr)
			p = &(*p)->rb_right;
		else
			return mp;
	}
	rb_link_node(node, parent, p);
	rb_insert_color(node, &mempage_root);
	return NULL;
}

static struct mempage *mempage_insert(u64 addr, struct mempage *mp)
{
	mp->addr = addr;
	mp = mempage_insert_lookup(addr, &mp->nd);
	return mp;
}

static void mempage_rb_tree_update(u64 addr, struct mempage *mp)
{
	rb_erase(&mp->nd, &mempage_root);
	mempage_insert(addr, mp);
}

static void mempage_cluster_lru_list_insert(struct mempage_cluster *mp_cluster)
{
	list_add(&mp_cluster->lru, &mempage_cluster_lru_list);
}

static void mempage_cluster_lru_list_update(struct mempage_cluster *mp_cluster)
{
	if (list_is_first(&mp_cluster->lru, &mempage_cluster_lru_list))
		return;

	list_del(&mp_cluster->lru);
	list_add(&mp_cluster->lru, &mempage_cluster_lru_list);
}

/* Following arrays need to be all kept in sync with the enum */

enum otype { 
	OFFLINE_OFF, 
	OFFLINE_ACCOUNT, 
	OFFLINE_SOFT, 
	OFFLINE_HARD,
	OFFLINE_SOFT_THEN_HARD 
};

static const char *kernel_offline[] = { 
	[OFFLINE_SOFT] = "/sys/devices/system/memory/soft_offline_page",
	[OFFLINE_HARD] = "/sys/devices/system/memory/hard_offline_page",
	[OFFLINE_SOFT_THEN_HARD] = "/sys/devices/system/memory/soft_offline_page"
};

static struct config_choice offline_choice[] = {
	{ "off", OFFLINE_OFF },
	{ "account", OFFLINE_ACCOUNT },
	{ "soft", OFFLINE_SOFT },
	{ "hard", OFFLINE_HARD },
	{ "soft-then-hard", OFFLINE_SOFT_THEN_HARD },
	{}
};

static enum otype offline = OFFLINE_OFF;

static int do_memory_offline(u64 addr, enum otype type)
{
	return sysfs_write(kernel_offline[type], "%#llx", addr);
}

static int memory_offline(u64 addr)
{
	if (offline == OFFLINE_SOFT_THEN_HARD) {
		if (do_memory_offline(addr, OFFLINE_SOFT) < 0)  { 
			Lprintf("Soft offlining of page %llx failed, trying hard offlining\n",
				addr);
			return do_memory_offline(addr, OFFLINE_HARD); 
		}
		return 0;
	}
	return do_memory_offline(addr, offline);
}

static void offline_action(struct mempage *mp, u64 addr)
{
	if (offline <= OFFLINE_ACCOUNT)
		return;
	Lprintf("Offlining page %llx\n", addr);
	if (memory_offline(addr) < 0) {
		Lprintf("Offlining page %llx failed: %s\n", addr, strerror(errno));
		mp->offlined = PAGE_OFFLINE_FAILED;
	} else
		mp->offlined = PAGE_OFFLINE;
}

/* Run a user defined trigger when the replacement threshold of page error counter crossed. */
static void counter_trigger(char *msg, time_t t, struct mempage_replacement *mr,
			    struct bucket_conf *bc, bool sync)
{
	struct leaky_bucket *bk = &mr->bucket;
	char *env[MAX_ENV], *out, *thresh;
	int i, ei = 0;

	thresh = bucket_output(bc, bk);
	xasprintf(&out, "%s: %s", msg, thresh);

	if (bc->log)
		Gprintf("%s\n", out);

	if (!bc->trigger)
		goto out;

	xasprintf(&env[ei++], "THRESHOLD=%s", thresh);
	xasprintf(&env[ei++], "TOTALCOUNT=%lu", mr->count);
	if (t)
		xasprintf(&env[ei++], "LASTEVENT=%lu", t);
	xasprintf(&env[ei++], "AGETIME=%u", bc->agetime);
	xasprintf(&env[ei++], "MESSAGE=%s", out);
	xasprintf(&env[ei++], "THRESHOLD_COUNT=%d", bk->count);
	env[ei] = NULL;
	assert(ei < MAX_ENV);

	run_trigger(bc->trigger, NULL, env, sync, "page-error-counter");

	for (i = 0; i < ei; i++)
		free(env[i]);
out:
	free(out);
	free(thresh);
}

void account_page_error(struct mce *m, int channel, int dimm)
{
	u64 addr = m->addr;
	struct mempage *mp;
	char *msg, *thresh;
	time_t t;
	unsigned cpu = m->extcpu ? m->extcpu : m->cpu;

	if (offline == OFFLINE_OFF)
		return;
	if (!(m->status & MCI_STATUS_ADDRV)  || (m->status & MCI_STATUS_UC))
		return;

	switch (cputype) {
	case CPU_SANDY_BRIDGE_EP:
		/*
		 * On SNB-EP platform we see corrected errors reported with
		 * address in Bank 5 from hardware (depending on BIOS setting),
                 * in the meanwhile, a duplicate record constructed from
                 * information found by "firmware first" APEI code. Ignore the
                 * duplicate information so that we don't double count errors.
		 *
		 * NOTE: the record from APEI fake this error from CPU 0 BANK 1.
		 */
		if (m->bank == 1 && cpu == 0)
			return;
	default:
		break;
	}

	t = m->time;
	addr &= ~((u64)PAGE_SIZE - 1);
	mp = mempage_lookup(addr);
	if (!mp && corr_err_counters < max_corr_err_counters) {
		mp = mempage_alloc();
		bucket_init(&mp->ce.bucket);
	        mempage_insert(addr, mp);
		mempage_cluster_lru_list_insert(to_cluster(mp));
		corr_err_counters++;
	} else if (!mp) {
		mp = mempage_replace();
		bucket_init(&mp->ce.bucket);
		mempage_rb_tree_update(addr, mp);
		mempage_cluster_lru_list_update(to_cluster(mp));

		/* Report how often the replacement of counter 'mp' happened */
		++mp_repalcement.count;
		if (__bucket_account(&mp_replacement_trigger_conf, &mp_repalcement.bucket, 1, t)) {
			thresh = bucket_output(&mp_replacement_trigger_conf, &mp_repalcement.bucket);
			xasprintf(&msg, "Replacements of page correctable error counter exceed threshold %s", thresh);
			free(thresh);

			counter_trigger(msg, t, &mp_repalcement, &mp_replacement_trigger_conf, false);
			free(msg);
		}
	} else {
		mempage_cluster_lru_list_update(to_cluster(mp));
	}
	++mp->ce.count;
	if (__bucket_account(&page_trigger_conf, &mp->ce.bucket, 1, t)) { 
		struct memdimm *md;

		if (mp->offlined != PAGE_ONLINE)
			return;
		/* Only do triggers and messages for online pages */
		thresh = bucket_output(&page_trigger_conf, &mp->ce.bucket);
		md = get_memdimm(m->socketid, channel, dimm, 1);
		xasprintf(&msg, "Corrected memory errors on page %llx exceed threshold %s",
			addr, thresh);
		free(thresh);
		memdb_trigger(msg, md, t, &mp->ce, &page_trigger_conf, NULL, false, "page");
		free(msg);
		mp->triggered = 1;

		if (offline == OFFLINE_SOFT || offline == OFFLINE_SOFT_THEN_HARD) {
			struct bucket_conf page_soft_trigger_conf;
			char *argv[] = {
				NULL,
				NULL,	
				NULL,
			};
			char *args;

			asprintf(&args, "%lld", addr);
			argv[0]=args;

			memcpy(&page_soft_trigger_conf, &page_trigger_conf, sizeof(struct bucket_conf));
			page_soft_trigger_conf.trigger = page_error_pre_soft_trigger;
			argv[0]=page_error_pre_soft_trigger;
			argv[1]=args;
			asprintf(&msg, "pre soft trigger run for page %lld", addr);
			memdb_trigger(msg, md, t, &mp->ce, &page_soft_trigger_conf, argv, true, "page_pre_soft");
			free(msg);

			offline_action(mp, addr);

			memcpy(&page_soft_trigger_conf, &page_trigger_conf, sizeof(struct bucket_conf));
			page_soft_trigger_conf.trigger = page_error_post_soft_trigger;
			argv[0]=page_error_post_soft_trigger;
			argv[1]=args;
			asprintf(&msg, "post soft trigger run for page %lld", addr);
			memdb_trigger(msg, md, t, &mp->ce, &page_soft_trigger_conf, argv, true, "page_post_soft");
			free(msg);
			free(args);

		} else
			offline_action(mp, addr);
	}
}

void dump_page_errors(FILE *f)
{
	char *msg;
	struct rb_node *r;
	long k;

	k = 0;
	for (r = rb_first(&mempage_root); r; r = rb_next(r)) { 
		struct mempage *p = rb_entry(r, struct mempage, nd);

		if (k++ == 0)
			fprintf(f, "Per page corrected memory statistics:\n");
		msg = bucket_output(&page_trigger_conf, &p->ce.bucket);
		fprintf(f, "%llx: total %u seen \"%s\" %s%s\n",
			p->addr,
			p->ce.count,
			msg,
			page_state[(unsigned)p->offlined],
			p->triggered ? " triggered" : "");
		free(msg);
		fputc('\n', f);
	}
}

void page_setup(void)
{
	int n;
	
	config_trigger("page", "memory-ce", &page_trigger_conf);
	config_trigger("page", "memory-ce-counter-replacement", &mp_replacement_trigger_conf);
	n = config_choice("page", "memory-ce-action", offline_choice);
	if (n >= 0)
		offline = n;
	if (offline > OFFLINE_ACCOUNT && 
	    !sysfs_available(kernel_offline[offline], W_OK)) {
		Lprintf("Kernel does not support page offline interface\n");
		offline = OFFLINE_ACCOUNT;
	}

	page_error_pre_soft_trigger = config_string("page", "memory-pre-sync-soft-ce-trigger");

	if (page_error_pre_soft_trigger && trigger_check(page_error_pre_soft_trigger) < 0) {
		SYSERRprintf("Cannot access page soft pre trigger `%s'",
				page_error_pre_soft_trigger);
		exit(1);
	}

	page_error_post_soft_trigger= config_string("page", "memory-post-sync-soft-ce-trigger");
	if (page_error_post_soft_trigger && trigger_check(page_error_post_soft_trigger) < 0) {
		SYSERRprintf("Cannot access page soft post trigger `%s'",
				page_error_post_soft_trigger);
		exit(1);
	}

	n = max_corr_err_counters;
	max_corr_err_counters = roundup(max_corr_err_counters, N);
	if (n != max_corr_err_counters)
		Lprintf("Round up max-corr-err-counters from %d to %d\n", n, max_corr_err_counters);

	bucket_init(&mp_repalcement.bucket);
}
