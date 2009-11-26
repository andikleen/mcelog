/* Copyright (C) 2006 Andi Kleen, SuSE Labs.
   Dumb database manager.
   not suitable for large datasets, but human readable files and simple.
   assumes groups and entries-per-group are max low double digits.
   the in memory presentation could be easily optimized with a few
   hashes, but that shouldn't be needed for now.
   Note: obsolete, new design uses in memory databases only

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
   add lock file to protect final rename
   timeout for locks
*/

#define _GNU_SOURCE 1
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <assert.h>

#include "db.h"
#include "memutil.h"

/* file format

# comment
[group1]
entry1: value
entry2: value

# comment
# comment2
[group2]
entry: value

value is anything before new line, but first will be skipped
spaces are allowed in entry names or groups
comments are preserved, but moved in front of the group
blank lines allowed.

code doesnt check for unique records/entries right now. first wins.

*/

struct entry { 
	char *name;
	char *val;
};
	
struct group {
	struct group *next;
	char *name;
	struct entry *entries;
	char *comment;
	int numentries;
};	

#define ENTRY_CHUNK (128 / sizeof(struct entry))

struct database {
	struct group *groups;
	FILE *fh;
	char *fn;
	int dirty;
}; 	

static int read_db(struct database *db);
static FILE *open_file(char *fn, int wr);
static void free_group(struct group *g);

static void DBerror(char *fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

#define DB_NEW(p) ((p) = xalloc(sizeof(*(p))))

static struct group *alloc_group(char *name)
{
	struct group *g;
	DB_NEW(g);
	g->entries = xalloc(ENTRY_CHUNK * sizeof(struct entry));
	g->name = name;
	return g;
}

static char *cleanline(char *s)
{
	char *p;
	while (isspace(*s))
		s++;
	if (*s == 0)
		return NULL;
	p = strchr(s, '\n');
	if (p)
		*p = 0;
	return s;	
}

struct database *open_db(char *fn, int wr)
{
	struct database *db;

	DB_NEW(db);
	db->fh = open_file(fn, wr);
	if (!db->fh) {
		DBerror("Cannot open database %s\n", fn);
		free(db);
		return NULL;
	}
	db->fn = xstrdup(fn);
	if (read_db(db) < 0) {
		free(db->fn);
		free(db);
		return NULL;
	}
	return db;
}

static int read_db(struct database *db)
{
	char *line = NULL;
	size_t linesz = 0;
	struct group *group = NULL, **pgroup = &db->groups;
	int linenr = 0;

	while (getline(&line, &linesz, db->fh) > 0) {
		char *s;
		s = strchr(line, '#');
		if (s) {
			struct group *cmt;
			DB_NEW(cmt);
			*pgroup = cmt;
			pgroup = &cmt->next;
			cmt->comment = xstrdup(s + 1);
			*s = 0;
		} 	
		s = cleanline(line);
		linenr++;
		if (!s) 	
			continue;
		if (*s == '[') {
			int n;
			char *name;
			++s;
			n = strcspn(s, "]");
			if (s[n] == 0)
				goto parse_error;
			name = xalloc(n + 1);
			memcpy(name, s, n);
			group = alloc_group(name);
			*pgroup = group;
			pgroup = &group->next;
		} else {
			char *p;
			if (!group)
				goto parse_error;
			p = s + strcspn(s, ":");
			if (*p != ':')
				goto parse_error;
			*p++ = 0;
			if (*p == ' ')
				p++;
			else
				goto parse_error;
			change_entry(db, group, line, p);
		}
	}								

	if (ferror(db->fh)) {
		DBerror("IO error while reading database %s: %s\n", db->fn,
			strerror(errno));
		goto error;
	}	
		
	free(line);
	return 0;

parse_error:
	DBerror("Parse error in database %s at line %d\n", db->fn, linenr);
error: 	
	free(line);
	return -1;
}

/*
Crash safety strategy:

While the database is opened hold a exclusive flock on the file
When writing write to a temporary file (.out).  Only when the file
is written rename to another temporary file (.complete).

Then sync and swap tmp file with main file, then sync directory
(later is linux specific)

During open if the main file doesn't exist and a .complete file does
rename the .complete file to main first; or open the .complete
file if the file system is read only.

*/

/* Flush directory. Useful on ext2, on journaling file systems
   the later fsync would usually force earlier transactions on the
   metadata too. */
static int flush_dir(char *fn)
{
	int err, fd;
	char *p;
	char dir[strlen(fn) + 1];
	strcpy(dir, fn);
	p = strrchr(dir, '/');
	if (p)
		*p = 0;
	else
		strcpy(dir, ".");
	fd = open(dir, O_DIRECTORY|O_RDONLY);
	if (fd < 0)
		return -1;
	err = 0;
	if (fsync(fd) < 0)
		err = -1;
	if (close(fd) < 0)
		err = -1;
	return err;
}

static int force_rename(char *a, char *b)
{
	unlink(b); /* ignore error */
	return rename(a, b);
}

static int rewrite_db(struct database *db)
{
	FILE *fhtmp;
	int err;

	int tmplen = strlen(db->fn) + 10;
	char fn_complete[tmplen], fn_old[tmplen], fn_out[tmplen];

	sprintf(fn_complete, "%s.complete", db->fn);
	sprintf(fn_old, "%s~", db->fn);
	sprintf(fn_out, "%s.out", db->fn);

	fhtmp = fopen(fn_out, "w");
	if (!fhtmp) {
		DBerror("Cannot open `%s' output file: %s\n", fn_out,
			strerror(errno));
		return -1;
	}	
	
	dump_database(db, fhtmp);
	
	err = 0;
	/* Finish the output file */
	if (ferror(fhtmp) || fflush(fhtmp) != 0 || fsync(fileno(fhtmp)) != 0 ||
	    fclose(fhtmp))
		err = -1;
	/* Rename to .complete */	
	else if (force_rename(fn_out, fn_complete))
		err = -1;
	/* RED-PEN: need to do retry for race */	
	/* Move to final name */
	else if (force_rename(db->fn, fn_old) || rename(fn_complete, db->fn))
		err = -1;
	/* Hit disk */	
	else if (flush_dir(db->fn))
		err = -1;
		
	if (err) {
		DBerror("Error writing to database %s: %s\n", db->fn,
				strerror(errno));
	}
				
	return err;			
}

int sync_db(struct database *db)
{
	if (!db->dirty)
		return 0;
	/* RED-PEN window without lock */
	if (rewrite_db(db))
		return -1;
	fclose(db->fh);		
	db->dirty = 0;
	db->fh = open_file(db->fn, 1);
	if (!db->fh)
		return -1;
	return 0;	
}

static void free_group(struct group *g)
{
	free(g->entries);
	free(g->name);
	free(g->comment);
	free(g); 	
}

static void free_data(struct database *db)
{
	struct group *g, *gnext;
	for (g = db->groups; g; g = gnext) {
		gnext = g->next;
		free_group(g);
	}
}

int close_db(struct database *db)
{
	if (db->dirty && rewrite_db(db))
		return -1;
	if (fclose(db->fh))
		return -1;
	free_data(db);
	free(db->fn);
	free(db);
	return 0; 	
}

static FILE *open_file(char *fn, int wr)
{
	char tmp[strlen(fn) + 10];
	FILE *fh;
	if (access(fn, wr ? (R_OK|W_OK) : R_OK)) {
		switch (errno) {
		case EROFS:
			wr = 0;
			break;
		case ENOENT:	
			/* No main DB file */
			sprintf(tmp, "%s.complete", fn);
			/* Handle race */
			if (!access(tmp, R_OK)) {
				if (rename(tmp, fn) < 0 && errno == EEXIST)
					return open_file(fn, wr);
			} else
				creat(fn, 0644);
			break;
		}
	}
	fh = fopen(fn, wr ? "r+" : "r");
	if (fh) {
		if (flock(fileno(fh), wr ? LOCK_EX : LOCK_SH) < 0) {
			fclose(fh);
			return NULL;
		}
	}	
	return fh;		
}

void dump_group(struct group *g, FILE *out)
{
	struct entry *e;
	fprintf(out, "[%s]\n", g->name);
	for (e = &g->entries[0]; e->name && !ferror(out); e++)
		fprintf(out, "%s: %s\n", e->name, e->val);
}

void dump_database(struct database *db, FILE *out)
{
	struct group *g; 	
	for (g = db->groups; g && !ferror(out); g = g->next) {
		if (g->comment) {
			fprintf(out, "#%s", g->comment);
			continue;
		}
		dump_group(g, out);
	}
}

struct group *find_group(struct database *db, char *name)
{
	struct group *g;
	for (g = db->groups; g; g = g->next)
		if (g->name && !strcmp(g->name, name))
			return g;
	return NULL;		
}

int delete_group(struct database *db, struct group *group)
{
	struct group *g, **gprev;
	gprev = &db->groups;
	for (g = *gprev; g; gprev = &g->next, g = g->next) {
		if (g == group) {
			*gprev = g->next;
			free_group(g);
			return 0;
		}
	}
	db->dirty = 1;
	return -1;		
}

char *entry_val(struct group *g, char *entry)
{
	struct entry *e;
	for (e = &g->entries[0]; e->name; e++) 
		if (!strcmp(e->name, entry))
			return e->val;
	return NULL;
}

struct group *add_group(struct database *db, char *name, int *existed)
{
	struct group *g, **gprev = &db->groups;
	for (g = *gprev; g; gprev = &g->next, g = g->next)
		if (g->name && !strcmp(g->name, name))
			break;
	if (existed)
		*existed = (g != NULL);
	if (!g) {
		g = alloc_group(xstrdup(name));
		g->next = *gprev;
		*gprev = g;
	}
	db->dirty = 1;
	return g;

}

void change_entry(struct database *db, struct group *g,
		     char *entry, char *newval)
{
	int i;
	struct entry *e, *entries;
	db->dirty = 1;
	entries = &g->entries[0];
	for (e = entries; e->name; e++) { 
		if (!strcmp(e->name, entry)) { 
			free(e->val);
			e->val = xstrdup(newval);
			return;
		}
	}
	i = e - entries;
	assert(i == g->numentries);
	if (i > 0 && (i % ENTRY_CHUNK) == 0) { 
		int new = (i + ENTRY_CHUNK) * sizeof(struct entry);
		g->entries = xrealloc(g->entries, new);
	} 
	entries = &g->entries[0];
	e = &entries[i];
	e->name = xstrdup(entry);
	e->val = xstrdup(newval);
	g->numentries++;
}

void delete_entry(struct database *db, struct group *g, char *entry)
{
	struct entry *e;
	for (e = &g->entries[0]; e->name; e++) 
		if (!strcmp(e->name, entry))
			break;
	if (e->name == NULL)
		return;
	while ((++e)->name)
		e[-1] = e[0];
	g->numentries--;
}

struct group *
clone_group(struct database *db, struct group *gold, char *newname)
{
	struct entry *e;
	struct group *gnew = add_group(db, newname, NULL);
	for (e = &gold->entries[0]; e->name; e++) 
		change_entry(db, gnew, e->name, e->val);
	return gnew;
}

static char *save_comment(char *c)
{
	int len = strlen(c);
	char *s = xalloc(len + 2); 	
	strcpy(s, c);
	if (len == 0 || c[len - 1] != '\n')
		s[len] = '\n';
	return s;	
}

void add_comment(struct database *db, struct group *group, char *comment)
{
	struct group *g;
	struct group **gprev = &db->groups;
	for (g = *gprev; g; gprev = &g->next, g = g->next) {
		if ((group && g == group) || (!group && g->comment == NULL))
			break;
	}
	DB_NEW(g);
	g->comment = save_comment(comment);
	g->next = *gprev;
	*gprev = g;
	db->dirty = 1;
}

struct group *first_group(struct database *db)
{
	return next_group(db->groups);
}

struct group *next_group(struct group *g)
{
	struct group *n;
	if (!g)
		return NULL;
	n = g->next;
	while (n && n->comment)
		n = n->next;
	return n;
}

char *group_name(struct group *g)
{
	return g->name;
}

struct group *find_entry(struct database *db, struct group *prev,
			 char *entry, char *value)
{
	int previ = 0; 
	struct entry *e;
	struct group *g;
	if (prev)
		g = prev->next;
	else
		g = db->groups;
	for (; g; g = g->next) {
		if (g->comment) 
			continue;
		/* Short cut when entry is at the same place as previous */
		if (previ < g->numentries) { 
			e = &g->entries[previ]; 
			if (!strcmp(e->name, entry)) {
				if (!strcmp(e->val, value))
					return g;
				continue;
			}
		}
		for (e = &g->entries[0]; e->name; e++) { 
			if (strcmp(e->name, entry)) 
				continue;
			if (!strcmp(e->val, value))
				return g;
			previ = e - &g->entries[0];
			break;
		}
	}
	return NULL;
}

void rename_group(struct database *db, struct group *g, char *newname)
{
	free(g->name);
	g->name = xstrdup(newname);
	db->dirty = 1;
}

unsigned long entry_num(struct group *g, char *entry)
{
	char *e = entry_val(g, entry);
	unsigned long val = 0;
	if (e)
		sscanf(e, "%lu", &val);
	return val;
}

void change_entry_num(struct database *db, struct group *g,
		      char *entry, unsigned long val)
{
	char buf[20];
	sprintf(buf, "%lu", val);
	change_entry(db, g, entry, buf);
}
