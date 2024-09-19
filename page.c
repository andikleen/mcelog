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
   be space efficiency. rbtree nodes are rather large.  */
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

/* sets up 2^12 = 4k BYTE page size*/

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)

/* a page can either be online or offline*/

enum { PAGE_ONLINE = 0, PAGE_OFFLINE = 1, PAGE_OFFLINE_FAILED = 2 };

/* represents a memory page, storing error-related information and its status online/offline*/
struct mempage { 
	struct rb_node nd;
	/* one char used by rb_node, node for integrating into red-black tree for efficent lookup */
	char offlined; //status of the page
	char triggered; //flag indicating if a trigger has been activated for this page
	// 1(32bit)-5(64bit) bytes of padding to play with here
	u64 addr;

	/*err_type stores leaky bucket and count; each page has a leaky bucket*/
	struct err_type ce;
	
};

#define N ((PAGE_SIZE - sizeof(struct list_head)) / sizeof(struct mempage))
#define to_cluster(mp)	(struct mempage_cluster *)((long)(mp) & ~((long)(PAGE_SIZE - 1)))

struct mempage_cluster { //a group of mempags
	struct list_head lru;
	struct mempage mp[N];
	int mp_used; //number of mempages in cluster
};

struct mempage_replacement { //tracks page replacements when error thresholds are exceeded
	struct leaky_bucket bucket;
	unsigned count;
};

enum {
	MAX_ENV = 20,
};

static int corr_err_counters;
static struct mempage_cluster *mp_cluster;
static struct mempage_replacement mp_repalcement;
static struct rb_root mempage_root; //red-black tree structure used to store and lookup mempages efficciently based on page addresses
static LIST_HEAD(mempage_cluster_lru_list);
static struct bucket_conf page_trigger_conf;
static struct bucket_conf mp_replacement_trigger_conf;
static char *page_error_pre_soft_trigger, *page_error_post_soft_trigger;

static const char *page_state[] = {
	[PAGE_ONLINE] = "online",
	[PAGE_OFFLINE] = "offline",
	[PAGE_OFFLINE_FAILED] = "offline-failed",
};

static struct mempage *mempage_alloc(void) //allocates new mempage from a cluster
{
	if (!mp_cluster || mp_cluster->mp_used == N) {
		mp_cluster = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mp_cluster == MAP_FAILED)
			Enomem();
	}

	return &mp_cluster->mp[mp_cluster->mp_used++];
}

static struct mempage *mempage_replace(void) //pages can be replaced if cluster is full
{
	struct mempage *mp;

	/* If no free mp_cluster, reuse the last mp_cluster of the LRU list  */
	if (mp_cluster->mp_used == N) {
		mp_cluster = list_last_entry(&mempage_cluster_lru_list, struct mempage_cluster, lru);
		mp_cluster->mp_used = 0;
	}

	mp = &mp_cluster->mp[mp_cluster->mp_used++];
	mp->offlined = PAGE_ONLINE;
	mp->triggered = 0;
	mp->ce.count = 0;

	return mp;
}

static struct mempage *mempage_lookup(u64 addr) //searches for a page in red-black tree
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

static struct mempage *
mempage_insert_lookup(u64 addr, struct rb_node * node) //finds the correct position in the tree and links the node
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

#pragma GCC diagnostic pop

static struct mempage *mempage_insert(u64 addr, struct mempage *mp) //inserts new mempage in red-black tree
{
	mp->addr = addr;
	mp = mempage_insert_lookup(addr, &mp->nd);
	return mp;
}

static void mempage_rb_tree_update(u64 addr, struct mempage *mp) //updates red black tree when replacing a mem page
{
	rb_erase(&mp->nd, &mempage_root);
	mempage_insert(addr, mp);
}
//LRU list management; manages LRU list by adding or moving mempage_cluster entries to maintain usage order
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

enum otype {  //different type of offlining strategies
	OFFLINE_OFF,  
	OFFLINE_ACCOUNT, 
	OFFLINE_SOFT, 
	OFFLINE_HARD,
	OFFLINE_SOFT_THEN_HARD //attempt soft offlining first then hard offliing if soft fails
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

static int do_memory_offline(u64 addr, enum otype type) //writes the memory page address to the appropriate sysfs entry to offline the page
{
	return sysfs_write(kernel_offline[type], "%#llx", addr);
}

static u64 get_first_memory_address(u64 addr) { //returns the memory addr at the start of the row that the input addr belongs too. The specific bits chosen were based off the system_addr decoding
    // Bitmask to clear bits 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, and 22 (the column address bits)
    u64 mask = ~((1ULL << 2) | (1ULL << 3) | (1ULL << 4) | (1ULL << 5) | (1ULL << 6) |
                 (1ULL << 9) | (1ULL << 10) | (1ULL << 11) | (1ULL << 12) | (1ULL << 13) | 
                 (1ULL << 22));
    
    // Apply mask to clear the specified bits
    return addr & mask;
}
//CHECKS IF FAILURE IS ROW FAILURE
static int is_row_failure(u64 addr) {
     // Step 1: Get the first memory address of the row.
    u64 first_row_addr = get_first_memory_address(addr);

    // Iterate over all 8 combinations of the 12th, 13th, and 22nd bits (3 bits => 8 combinations)
    int triggered_pages = 0;
    for (int i = 0; i < 8; i++) {
        u64 modified_addr = first_row_addr;

        // Set or clear the 12th bit (based on the least significant bit of 'i')
        if (i & 0x1) {
            modified_addr |= (1ULL << 12);  // Set the 12th bit to 1
        } else {
            modified_addr &= ~(1ULL << 12); // Set the 12th bit to 0
        }

        // Set or clear the 13th bit (based on the second least significant bit of 'i')
        if (i & 0x2) {
            modified_addr |= (1ULL << 13);  // Set the 13th bit to 1
        } else {
            modified_addr &= ~(1ULL << 13); // Set the 13th bit to 0
        }

        // Set or clear the 22nd bit (based on the third least significant bit of 'i')
        if (i & 0x4) {
            modified_addr |= (1ULL << 22);  // Set the 22nd bit to 1
        } else {
            modified_addr &= ~(1ULL << 22); // Set the 22nd bit to 0
        }

        // Step 2: Look up the page corresponding to the modified address.
        struct mempage *page = mempage_lookup(modified_addr);
        
        // Step 3: If the page exists and has triggered the threshold, count it as a failure.
        if (page && page->triggered) {
            triggered_pages++;
        }
    }

    // Step 4: Check if alteast half pages in the row have triggered the threshold.
    // Here, we consider a row failure if alteast half of the 8 possible pages (at least 4) have triggered errors. SUBJECT TO CHANGE BASED ON FEEDBACK
    return triggered_pages >= 4;
}


//HANDLES THE ROW FAILURE
static int row_failure_handler(u64 addr, enum otype type) 
{
    u64 first_row_addr = get_first_memory_address(addr);  // Get the first memory address of the row

    // Iterate over all 8 combinations of the 12th, 13th, and 22nd bits (3 bits => 8 combinations)
    for (int i = 0; i < 8; i++) {
        u64 modified_addr = first_row_addr;

        // Set or clear the 12th bit (based on the least significant bit of 'i')
        if (i & 0x1) {
            modified_addr |= (1ULL << 12);  // Set the 12th bit to 1
        } else {
            modified_addr &= ~(1ULL << 12); // Set the 12th bit to 0
        }

        // Set or clear the 13th bit (based on the second least significant bit of 'i')
        if (i & 0x2) {
            modified_addr |= (1ULL << 13);  // Set the 13th bit to 1
        } else {
            modified_addr &= ~(1ULL << 13); // Set the 13th bit to 0
        }

        // Set or clear the 22nd bit (based on the third least significant bit of 'i')
        if (i & 0x4) {
            modified_addr |= (1ULL << 22);  // Set the 22nd bit to 1
        } else {
            modified_addr &= ~(1ULL << 22); // Set the 22nd bit to 0
        }

        // Find the corresponding page for the modified address
        struct mempage *page = mempage_lookup(modified_addr);
        if (page == NULL) {
            Lprintf("Page lookup for address %llx failed\n", modified_addr);
            return -1;
        }

        // Try to offline the page
        if (do_memory_offline(modified_addr, type) < 0) {
            Lprintf("Offlining page %llx failed\n", modified_addr);
            return -1;
        }
    }

    return 0;  // Return 0 if all pages were successfully offlined
}



static int memory_offline(u64 addr) /*determines offlining strategy based on offline mode and attemps to offline the page. If mode is offlining_soft_then_hard, it first tries 
 soft offlining. If soft offlining fails, it attempts hard offlining. */
{


	if (offline == OFFLINE_SOFT_THEN_HARD) {
		if (do_memory_offline(addr, OFFLINE_SOFT) < 0)  { 
			Lprintf("Soft offlining of page %llx failed, trying hard offlining\n",
				addr);
			return do_memory_offline(addr, OFFLINE_HARD); 
		}
		return 0;
	}
	//return do_memory_offline(addr, offline); MODIFICATION - LINE HAS BEEN COMMENTED OUT

	if (is_row_failure(addr)) {
        // If row failure is detected, offline consecutive pages
        return row_failure_handler(addr, OFFLINE_SOFT);
    } else {
        // If not a row failure (COL or BLK fail or just 1/2 memory addresses right next to eachother producing lots of errors), offline a single page using the configured offline mode
        return do_memory_offline(addr, offline);
	}
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

	for (i = 0; i < ei; i++) {
		free(env[i]);
		env[i] = NULL;
	}
out:
	free(out);
	out = NULL;
	free(thresh);
	thresh = NULL;
}

void account_page_error(struct mce *m, int channel, int dimm) //core function that handles each memory error reported by system
{
	u64 addr = m->addr;
	struct mempage *mp;
	char *msg, *thresh;
	time_t t;
	unsigned cpu = m->extcpu ? m->extcpu : m->cpu;

	if (offline == OFFLINE_OFF)
		return; //exit if offlining disabled
	if (!(m->status & MCI_STATUS_ADDRV)  || (m->status & MCI_STATUS_UC)) //check if error has valid address
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
	//rounds down to nearest page size boundary
	addr &= ~((u64)PAGE_SIZE - 1);
	mp = mempage_lookup(addr); //attempt to find an existing mempage for the address
	if (!mp && corr_err_counters < max_corr_err_counters) { //if not found, allocate a new mempage, initialize its bucket, insert into red-black tree and LRU list increment error counter
		//max_corr_err_counters is the max number of correctable error pages that can be tracked. The variable corr_err_counters keeps track of the current number of correctable error pages

		mp = mempage_alloc();
		bucket_init(&mp->ce.bucket);
	        mempage_insert(addr, mp);
		mempage_cluster_lru_list_insert(to_cluster(mp));
		corr_err_counters++;
	} else if (!mp) { //if not found and maximum counters reached, replace an existing mempage, initialize its bucket...etc.
		mp = mempage_replace();
		bucket_init(&mp->ce.bucket);
		mempage_rb_tree_update(addr, mp);
		mempage_cluster_lru_list_update(to_cluster(mp));

		/* Report how often the replacement of counter 'mp' happened */
		++mp_repalcement.count; //tracks how often old error pages have been replaced by new ones
		if (__bucket_account(&mp_replacement_trigger_conf, &mp_repalcement.bucket, 1, t)) {
			thresh = bucket_output(&mp_replacement_trigger_conf, &mp_repalcement.bucket);
			xasprintf(&msg, "Replacements of page correctable error counter exceed threshold %s", thresh);
			free(thresh);
			thresh = NULL;

			counter_trigger(msg, t, &mp_repalcement, &mp_replacement_trigger_conf, false);
			free(msg);
			msg = NULL;
		}
	} else {
		mempage_cluster_lru_list_update(to_cluster(mp));
	}
	//increment error count for page -> adding to its bucket
	++mp->ce.count;
	//checks if number of errors on page exceeds threshold using __bucket_account function..(page_trigger_conf kinda important for defining threshold?)
	if (__bucket_account(&page_trigger_conf, &mp->ce.bucket, 1, t)) { 
		struct memdimm *md;
		//if page has already been offlined, skip rest of code
		if (mp->offlined != PAGE_ONLINE)
			return;
		/* Only do triggers and messages for online pages. 
		This generates a message that includes the number of errors and the time window during which they occurred.*/
		thresh = bucket_output(&page_trigger_conf, &mp->ce.bucket);
		md = get_memdimm(m->socketid, channel, dimm, 1);
		xasprintf(&msg, "Corrected memory errors on page %llx exceed threshold %s",
			addr, thresh);
		free(thresh);
		thresh = NULL;
		memdb_trigger(msg, md, t, &mp->ce, &page_trigger_conf, NULL, false, "page");
		free(msg);
		msg = NULL;
		mp->triggered = 1; // marks that the page has triggered this threshold-based error handling

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
			msg = NULL;

			offline_action(mp, addr);

			memcpy(&page_soft_trigger_conf, &page_trigger_conf, sizeof(struct bucket_conf));
			page_soft_trigger_conf.trigger = page_error_post_soft_trigger;
			argv[0]=page_error_post_soft_trigger;
			argv[1]=args;
			asprintf(&msg, "post soft trigger run for page %lld", addr);
			memdb_trigger(msg, md, t, &mp->ce, &page_soft_trigger_conf, argv, true, "page_post_soft");
			free(msg);
			msg = NULL;
			free(args);
			args = NULL;
		} else
			offline_action(mp, addr);
	}
}

void dump_page_errors(FILE *f) //outputs current state of memory page errors to a file
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
		msg = NULL;
		fputc('\n', f);
	}
}

void page_setup(void) //sets up various configurations
{
	int n;
	
	config_trigger("page", "memory-ce", &page_trigger_conf);
	config_trigger("page", "memory-ce-counter-replacement", &mp_replacement_trigger_conf);
	n = config_choice("page", "memory-ce-action", offline_choice);
	if (n >= 0) //choosing offling action
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
	max_corr_err_counters = roundup(max_corr_err_counters, N); //adjusts max_corr_err_counters to the nearest multipl of N
	if (n != max_corr_err_counters)
		Lprintf("Round up max-corr-err-counters from %d to %d\n", n, max_corr_err_counters);

	bucket_init(&mp_repalcement.bucket);
}
