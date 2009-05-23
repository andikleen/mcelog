/* Copyright (C) 2009 Intel Corporation
   Simple config file parser

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
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 

   Author: Andi Kleen 
*/
#define _GNU_SOURCE 1
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <getopt.h>
#include "memutil.h"
#include "mcelog.h"
#include "config.h"

#ifdef TEST
#define Eprintf printf
#define Wprintf printf
#define xalloc(x) calloc(x,1)
#endif

/* ISSUES: 
   doesn't support white space in fields
   doesn't merge/detect duplicated headers */

#define SHASH 11

struct opt { 
	struct opt *next;
	char *name;
	char *val;
};

struct header { 
	struct header *next;
	char *name;
	struct opt *opts[SHASH];
	struct opt *optslast[SHASH];
};

static struct header *hlist;

/* djb hash */
static unsigned hash(const char *str)
{
	const unsigned char *s;
        unsigned hash = 5381;
	for (s = (const unsigned char *)str; *s; s++) 
		hash = (hash * 32) + hash + *s;
        return hash % SHASH;
}

static struct header *new_header(struct header *prevh, char *name)
{
	struct header *h = xalloc(sizeof(struct header));
	h->name = name;
	if (prevh) 
		prevh->next = h;
	else 
		hlist = h;
	return h;
}

static int empty(char *s)
{
	while (isspace(*s))
		++s;
	return *s == 0;
}

static void parse_error(int line, char *msg)
{
	Eprintf("config file line %d: %s\n", line, msg);
	exit(1);
}

static void nothing(char *s, int line)
{
	if (!empty(s) != 0)
		parse_error(line, "left over characters at end of line");
}

static void unparseable(char *desc, const char *header, const char *name)
{
	char *sep = ":";
	if (!strcmp(header, "global")) {
		header = "";
		sep = "";
	}
	Wprintf("%s config option ``%s%s%s'' unparseable\n", 
			desc, header, sep, name);
}

int parse_config_file(const char *fn)
{
	FILE *f;
	char *line = NULL;
	size_t linelen = 0;

	char *header;
	char *name;
	char *val;
	struct opt *opt;
	struct header *hdr;
	int lineno = 1;
	int left;
	unsigned h;

	f = fopen(fn, "r");
	if (!f)
		return -1;

	hdr = NULL;
	while (getline(&line, &linelen, f) > 0) {
		char *s;
		s = strchr(line, '#');
		if (s) 
			*s = 0;
		if (sscanf(line, " [%a[^]]]%n", &header, &left) == 1) { 
			nothing(line + left, lineno);
			hdr = new_header(hdr, header);
		} else if (sscanf(line, " %as = %as%n", &name, &val, &left) == 2) {
			nothing(line + left, lineno);	
			opt = xalloc(sizeof(struct opt));
			opt->name = name;
			opt->val = val;
			h = hash(name);
			if (!hdr) 
				hdr = new_header(hdr, "global");
			if (hdr->optslast[h] == NULL) 
				hdr->opts[h] = opt;
			else
				hdr->optslast[h]->next = opt;
			hdr->optslast[h] = opt;
		} else if (!empty(line)) { 
			parse_error(lineno, "config file line not field nor header");
		}
		lineno++;
	}
	free(line);
	fclose(f);
	return 0;
}

char *config_string(const char *header, const char *name)
{
	struct header *hdr;	
	unsigned h = hash(name);
	for (hdr = hlist; hdr; hdr = hdr->next) {
		if (!strcmp(hdr->name, header)) { 
			struct opt *o;	
			for (o = hdr->opts[h]; o; o = o->next) { 
				if (!strcmp(o->name, name))
					return o->val;
			}
		}
	}
	if (strcmp(header, "global"))
		return config_string("global", name);
	return NULL;
}

int config_number(const char *header, const char *name, char *fmt, void *val)
{
	char *str = config_string(header, name);
	if (str == NULL)
		return -1;
	if (sscanf(str, fmt, val) != 1) { 
		unparseable("numerical", header, name);
		return -1;
	}
	return 0;
}

int config_choice(const char *header, const char *name, const struct config_choice *c)
{
	char *str = config_string(header, name);
	if (!str)
		return -1;
	for (; c->name; c++) {
		if (!strcasecmp(str, c->name))
			return c->val;
	}
	unparseable("choice", header, name);
	return -1;
}

int config_bool(const char *header, const char *name)
{
	static const struct config_choice bool_choices[] = {
		{ "yes", 1 }, { "true", 1 }, { "1", 1 }, { "on", 1 },
		{ "no", 0 }, { "false", 0 }, { "0", 0 }, { "off", 0 },
		{}
	};	
	return config_choice(header, name, bool_choices);
}

/* Look for the config file argument before parsing the other 
   options because we want to read the config file first so 
   that command line options can conveniently override it. */
const char *config_file(char **av, const char *deffn)
{
	while (*++av) { 
		if (!strcmp(*av, "--"))
			break;
		if (!strncmp(*av, "--config-file", 13)) {
			if ((*av)[13] == '=') {
				return 14 + *av;
			} else { 
				return av[1];
			}
		}
	}
	return deffn;
}

/* Use getopt_long struct option array to process config file */
void config_options(struct option *opts, int (*func)(int))
{
	for (; opts->name; opts++) {
		if (!opts->has_arg) {
			if (config_bool("global", opts->name) != 1)
				continue;
			if (opts->flag) {
				*(opts->flag) = opts->val;	
				continue;
			}
		} else {
			char *s = config_string("global", opts->name);
			if (s == NULL)
				continue;
			optarg = s;
		}
		func(opts->val);
	}
}

#ifdef TEST
int main(int ac, char **av)
{
	if (!av[1]) 
		printf("need config file\n"), exit(1);
	if (parse_config_file(av[1]) < 0)
		printf("cannot parse config file\n"), exit(1);
	char *type;
	char *header;
	char *name;
	int n;
	while (scanf("%as %as %as", &type, &header, &name) == 3) { 
		switch (type[0]) { 
		case 'n':
			if (config_number(header, name, "%d", &n) < 0)
				printf("Cannot parse number %s %s\n", header, name);
			else
				printf("res %d\n", n);
			break;
		case 's':
			printf("res %s\n", config_string(header, name));
			break;
		case 'b':
			printf("res %d\n", config_bool(header, name));
			break;
		default:
			printf("unknown type %s\n", type);
			break;
		}
		free(type);
		free(header);
		free(name);
	} 
	return 0;
}
#endif
