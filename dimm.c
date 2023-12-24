/* Copyright (C) 2006 Andi Kleen, SuSE Labs.
   Manage dimm database.
   this is used to keep track of the error counts per DIMM
   so that we can take action when one starts to experience a 
   unusual large number of them.
   Note: obsolete, not used anymore, new design is in memdb.c

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

/* TBD:
   Put error trigger information into database? */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include "dmi.h"
#include "mcelog.h"
#include "db.h"
#include "dimm.h"

/* the algorithms are mostly brute force, only the generally small number of 
   dimms saves us.
   advantage it is a quite simple and straight forward. */

struct database *dimm_db;

struct key {
	char *name;
	size_t offset;
	enum { D_STR, D_BYTE, D_WORD, D_SIZE } type;
	int cmp;
};

static unsigned key_sizes[] = {
	[D_STR] = 1,
	[D_BYTE] = 1,
	[D_WORD] = 2,
	[D_SIZE] = 2,
};

#define O(x) offsetof(struct dmi_memdev, x)

static struct key keys[] = {
	{ "Locator", O(device_locator), D_STR, 0 },
	{ "Manufacturer", O(manufacturer), D_STR, 1},
	{ "Serial Number", O(serial_number),D_STR, 1},
	{ "Part Number", O(part_number), D_STR,10},
	{ "Asset Tag", O(asset_tag), D_STR, 0},
	{ "Speed", O(speed), D_WORD, 0},
	{ "Size", O(speed), D_SIZE, 1},
	{ "Form Factor", O(form_factor), D_STR, 0 },
	{ "Type Details", O(type_details), D_WORD, 1 },
	{ "Memory Type", O(memory_type), D_BYTE, 1 },
	{ "Total Width", O(total_width), D_WORD, 0 },
	{ "Data Width", O(data_width), D_WORD, 0 },
	{ "Device Set", O(device_set), D_BYTE, 0 },
	{ "Handle", O(header.handle), D_WORD, 0 },
	{ "Bank Locator", O(bank_locator), D_STR, 0 },
	{ "Array Handle", O(array_handle), D_WORD, 0 },
	{ },
};

static void fmt_size(struct dmi_memdev *a, char *buf)
{
	char *unit;
	unit = buf + sprintf(buf, "%u ", a->size);
	dmi_dimm_size(a->size, unit);
	*++unit = 0;	
}

static char *d_string(struct dmi_memdev *d, struct key *k, char *buf)
{
	unsigned char *p;
	if (k->offset + key_sizes[k->type] > d->header.length)
		return NULL;
	p = (unsigned char *)d + k->offset;
	switch (k->type) {
	case D_BYTE:
		sprintf(buf, "%u", *p);
		break;
	case D_WORD:
		sprintf(buf, "%u", *(unsigned short *)p);
		break;
	case D_STR:
		return dmi_getstring(&d->header, *p);
	case D_SIZE:
		fmt_size(d, buf);
		break;
	default:
		abort();
	}
	return buf;
}

static int cmp_dimm(struct dmi_memdev *a, struct group *b)
{
	int i;
	for (i = 0; keys[i].name; i++) {
		char buf[100];
		struct key *k = &keys[i];
		if (!k->cmp)
			continue;
		char *s = d_string(a, k, buf);
		if (!s)
			continue;
		char *s2 = entry_val(b, k->name);
		if (!s2)
			continue;
		if (strcmp(s, s2))
			return 0;
	}
	return 1;
}

static void d_to_group(struct dmi_memdev *de, struct group *g)
{
	char buf[100];
	int i;
	for (i = 0; keys[i].name; i++) {
		struct key *k = &keys[i];
		char *s = d_string(de, k, buf);
		if (s)
			change_entry(dimm_db, g, k->name, s);
	}		
}

/* TBD get this into syslog somehow without spamming? */
static void unique_warning(void)
{
	static int warned;
	if (warned)
		return;
	warned = 1;
	Wprintf("Cannot uniquely identify your memory modules\n");
	Wprintf("When changing them you should manage them using command line mcelog\n");
}

static struct dmi_memdev *matching_dimm_group(struct group *g)
{
	int i;
	struct dmi_memdev *match = NULL;
	int nmatch = 0;
	for (i = 0; dmi_dimms[i]; i++) {
		if (cmp_dimm(dmi_dimms[i], g)) {
			match = dmi_dimms[i];
			nmatch++;
		}
	}
	if (nmatch > 1) { 
		unique_warning();
		return NULL;
	}
	return match;
}

static struct group *matching_dimm_dmi(struct dmi_memdev *d)
{
	struct group *match = NULL, *g;
	int nmatch = 0;
	for (g = first_group(dimm_db); g; g = next_group(g)) {
		if (!cmp_dimm(d, g)) { 
			match = g;
			nmatch++;
		}
	} 
	if (nmatch > 1) { 
		unique_warning();
		return NULL;
	}
	return match;
}

void create_dimm_name(struct dmi_memdev *d, char *buf)
{
	int i = 1;
	do {
		sprintf(buf, "dimm%d", i++);
	} while (find_group(dimm_db, buf));
}

static char *timestamp(void)
{
	static char buf[20];
	time_t now;
	time(&now);
	sprintf(buf, "%lu", now);
	return buf;
}

static void remove_dimm(struct group *g)
{
	char *loc = entry_val(g, "Locator");
	Wprintf("Removing %s who was at %s\n", group_name(g), loc);
	change_entry(dimm_db, g, "old locator", loc);
	change_entry(dimm_db, g, "Locator", "removed");
	change_entry(dimm_db, g, "removed at", timestamp());
}

static void disable_leftover_dimms(void)
{
	int i;
	struct group *g;
	/* Disable any left over dimms in the database.
	   don't remove them because the information might
	   be still useful later */
	for (g = first_group(dimm_db); g; g = next_group(g)) {
		char *gloc = entry_val(g, "Locator");
		if (!gloc || !strcmp(gloc, "removed"))
			continue;
		for (i = 0; dmi_dimms[i]; i++) {
			struct dmi_memdev *d = dmi_dimms[i];
			char *loc = dmi_getstring(&d->header,
						  d->device_locator);
			if (!strcmp(loc, gloc))
				break;
		}
		if (dmi_dimms[i] == NULL)
			remove_dimm(g);
	}
}

void move_dimm(struct group *g, struct dmi_memdev *newpos, char *loc)
{
	char *newloc = dmi_getstring(&newpos->header, newpos->device_locator);
	Wprintf("%s seems to have moved from %s to %s\n",
		group_name(g), loc, newloc);
	change_entry(dimm_db, g, "old locator", loc);
	change_entry(dimm_db, g, "Locator", newloc);
	delete_entry(dimm_db, g, "removed at");
	change_entry(dimm_db, g, "moved at", timestamp());
}			

void new_dimm(struct dmi_memdev *d, char *loc)
{
	struct group *g;
	char name[100];
	create_dimm_name(d, name);
	g = add_group(dimm_db, name, NULL);
	d_to_group(d, g);
	change_entry(dimm_db, g, "added at", timestamp());
	Wprintf("Found new %s at %s\n", name, loc);
	/* Run uniqueness check */
	(void)matching_dimm_group(g);
}	

/* check if reported dimms are at their places */
void check_dimm_positions(void)
{
	int i;
	struct group *g;
	struct dmi_memdev *d;
	struct dmi_memdev *match;

	for (i = 0; (d = dmi_dimms[i]) != NULL; i++) {
		char *loc = dmi_getstring(&d->header, d->device_locator);
		g = find_entry(dimm_db, NULL, "Locator", loc);
		/* In the database, but somewhere else? */
		if (g && !cmp_dimm(d, g)) {
			match = matching_dimm_group(g); 
			if (match)
				move_dimm(g, match, loc);
			else
				remove_dimm(g);
			g = NULL;
		/* In DMI but somewhere else? */
		} else if (!g) { 
			g = matching_dimm_dmi(d);
			if (g) 
				move_dimm(g, d, loc);
		} 
		if (!g)
			new_dimm(d, loc);
	}
}

/* synchronize database with smbios */
int sync_dimms(void)
{
	if (!dmi_dimms)
		return -1;
	check_dimm_positions();
	disable_leftover_dimms();	
	sync_db(dimm_db);
	return 0;	
}

void gc_dimms(void)
{
	struct group *g;
	while ((g = find_entry(dimm_db, NULL, "Locator", "removed")) != NULL) {
		Wprintf("Purging removed %s which was at %s\n",
			group_name(g), entry_val(g, "Old Locator"));
		delete_group(dimm_db, g);
	}
	sync_db(dimm_db);
}

static unsigned long inc_val(struct group *g, char *entry)
{
	unsigned long val = entry_num(g, entry) + 1;
	change_entry_num(dimm_db, g, entry, val);
	return val;
}

static void run_trigger(char *trigger, char *loc, unsigned long val,
			unsigned long max)
{
	pid_t pid;

	Lprintf("Running error trigger because memory at %s had %lu errors\n",
		loc, max);
	close_dimm_db();
	if ((pid = fork()) == 0) {
		char valbuf[20], maxbuf[20];
		char *argv[] = {
			trigger,
			loc,
			valbuf,
			maxbuf,
			NULL
		};
		char *env[] = {
			"PATH=/sbin:/usr/bin",
			NULL
		};
		sprintf(valbuf, "%lu", val);
		sprintf(maxbuf, "%lu", max);
		execve(trigger, argv, env);
		_exit(1);
	}
	int status;
	if (waitpid(pid, &status, 0) ||
	    !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		Eprintf("Cannot run error trigger %s for %s\n", trigger, loc);
	open_dimm_db(NULL);
}
void new_error(unsigned long long addr, unsigned long max_error, char *trigger)
{
	struct dmi_memdev **devs;
	int i;

	devs = dmi_find_addr(addr);
	if (devs[0] == NULL) {
		Wprintf("No memory found for address %llx\n", addr);
		exit(1);
	}
	for (i = 0; devs[i]; i++) {
		struct dmi_memdev *d = devs[i];
		char *loc = dmi_getstring(&d->header, d->device_locator);
		struct group *g = find_entry(dimm_db, NULL, "Locator", loc);
		if (!g) { // shouldn't happen
			Eprintf("No record found for %llx\n", addr);
			return;
		}
		unsigned long val = inc_val(g, "corrected errors");
		if (val == max_error) { 
			Lprintf("Large number of corrected errors in memory at %s", loc);
			Lprintf("Consider replacing it");
			if (trigger && trigger[0])
				run_trigger(trigger, loc, val, max_error, false, "dimm");
		}
	}
	free(devs);
	devs = NULL;
}

void reset_dimm(char *locator)
{
	struct group *g;
	if (locator) {
		g = find_entry(dimm_db, NULL, "Locator", locator);
		if (!g) {
			fprintf(stderr, "Locator %s not found\n", locator);
			exit(1);
		}
		change_entry(dimm_db, g, "corrected errors", "0");
	} else {
		for (g = first_group(dimm_db); g; g = next_group(g))
			change_entry(dimm_db, g, "corrected errors", "0");
	}
	sync_db(dimm_db);
}

struct group *lookup_dimm(char *locator)
{
	struct group *g = find_entry(dimm_db, NULL, "Locator", locator);
	return g;
}

void dump_all_dimms(void)
{
	dump_database(dimm_db, stdout);
}

void dump_dimm(char *locator)
{
	struct group *g = lookup_dimm(locator);
	if (g)
		dump_group(g, stdout);
	else
		fprintf(stderr, "%s not found\n", locator);
}

void close_dimm_db(void)
{
	if (dimm_db) {
		close_db(dimm_db);
		dimm_db = NULL;
	}
}

int open_dimm_db(char *fn)
{
	static char *old_db_name;
	if (dmi_dimms < 0)
		return -1;
	if (dimm_db)
		return 0;
	if (!fn) {
		fn = old_db_name;
	} else {
		old_db_name = strdup(fn);
		if (!old_db_name)
			exit(ENOMEM);
		atexit(close_dimm_db);
	}
	dimm_db = open_db(fn, 1);
	if (!dimm_db) {
		Eprintf("Cannot open dimm database %s: %s", fn,
			strerror(errno));
		return -1;
	}
	if (sync_dimms() < 0)
		return -1;
	return 0;
}
