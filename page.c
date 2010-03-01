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
#include "memutil.h"
#include "mcelog.h"
#include "rbtree.h"
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
	u64 addr;
	struct err_type ce;
	char offlined;
	char triggered;
	// 2(32bit)-6(64bit) bytes of padding to play with here
};

static struct rb_root mempage_root;
static struct bucket_conf page_trigger_conf;

static const char *page_state[] = {
	[PAGE_ONLINE] = "online",
	[PAGE_OFFLINE] = "offline",
	[PAGE_OFFLINE_FAILED] = "offline-failed",
};

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
	return NULL;
}

static struct mempage *mempage_insert(u64 addr, struct mempage *mp)
{
	mp->addr = addr;
	mp = mempage_insert_lookup(addr, &mp->nd);
	if (mp != NULL)
		rb_insert_color(&mp->nd, &mempage_root);
	return mp;
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

void account_page_error(struct mce *m, int channel, int dimm)
{
	u64 addr = m->addr;
	struct mempage *mp;
	time_t t;

	if (offline == OFFLINE_OFF)
		return;
	if (!(m->status & MCI_STATUS_ADDRV)  || (m->status & MCI_STATUS_UC))
		return;

	t = m->time;
	addr &= ~((u64)PAGE_SIZE - 1);
	mp = mempage_lookup(addr);
	if (!mp) {
		mp = xalloc(sizeof(struct mempage));
		bucket_init(&mp->ce.bucket);
	        mempage_insert(addr, mp);
	}
	++mp->ce.count;
	if (__bucket_account(&page_trigger_conf, &mp->ce.bucket, 1, t)) { 
		struct memdimm *md;
		char *msg;
		char *thresh;

		if (mp->offlined != PAGE_ONLINE)
			return;
		/* Only do triggers and messages for online pages */
		thresh = bucket_output(&page_trigger_conf, &mp->ce.bucket);
		md = get_memdimm(m->socketid, channel, dimm);
		asprintf(&msg, "Corrected memory errors on page %llx exceed threshold %s",
			addr, thresh);
		free(thresh);
		memdb_trigger(msg, md, t, &mp->ce, &page_trigger_conf);
		free(msg);
		mp->triggered = 1;
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
		fprintf(f, "%llx: total %lu seen \"%s\" %s%s\n",
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
	n = config_choice("page", "memory-ce-action", offline_choice);
	if (n >= 0)
		offline = n;
	if (offline > OFFLINE_ACCOUNT && 
	    !sysfs_available(kernel_offline[offline], W_OK)) {
		Lprintf("Kernel does not support page offline interface\n");
		offline = OFFLINE_ACCOUNT;
	}
}
