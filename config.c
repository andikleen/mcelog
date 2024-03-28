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
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include "memutil.h"
#include "mcelog.h"
#include "config.h"
#include "leaky-bucket.h"
#include "trigger.h"

#ifdef TEST
#define Eprintf printf
#define Wprintf printf
#define xalloc(x) calloc(x,1)
#endif

/* ISSUES: 
   doesn't detect misspelled options (this would require a major revamp!)
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
	h->name = xstrdup(name);
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

static void noreturn parse_error(int line, char *msg)
{
	Eprintf("config file line %d: %s\n", line, msg);
	exit(1);
}

static void nothing(char *s, int line)
{
	if (!empty(s))
		parse_error(line, "left over characters at end of line");
}

static void unparseable(char *desc, const char *header, const char *name)
{
	char *field;

	if (!strcmp(header, "global")) { 
		xasprintf(&field, "%s", name);
	} else { 
		xasprintf(&field, "[%s] %s", header, name);
	}
	Eprintf("%s config option `%s' unparseable\n", desc, field);
	free(field);
	field = NULL;
	exit(1);
}

/* Remove leading/trailing white space */
static char *strstrip(char *s)
{
	char *p;
	while (isspace(*s))
		s++;
	p = s + strlen(s) - 1;
	if (p <= s)
		return s;
	while (isspace(*p) && p >= s)
		*p-- = 0;
	return s;
}

int parse_config_file(const char *fn)
{
	FILE *f;
	char *line = NULL;
	size_t linelen = 0;

	char *name;
	char *val;
	struct opt *opt;
	struct header *hdr;
	int lineno = 1;
	unsigned h;

	f = fopen(fn, "r");
	if (!f)
		return -1;

	hdr = NULL;
	while (getline(&line, &linelen, f) > 0) {
		char *s = strchr(line, '#');
		if (s) 
			*s = 0;
		s = strstrip(line);
		if (*s == '[') {
			char *p = strchr(s, ']'); 
			if (p == NULL)
				parse_error(lineno, "Header without ending ]");
			nothing(p + 1, lineno);
			*p = 0;
			hdr = new_header(hdr, s + 1);
		} else if ((val = strchr(line, '=')) != NULL) { 
			*val++ = 0;
			name = strstrip(s);
			val = strstrip(val);
			opt = xalloc(sizeof(struct opt));
			opt->name = xstrdup(name);
			opt->val = xstrdup(val);
			h = hash(name);
			if (!hdr) 
				hdr = new_header(hdr, "global");
			//printf("[%s] \"%s\" = \"%s\"\n", hdr->name, name, val);
			if (hdr->optslast[h] == NULL) 
				hdr->opts[h] = opt;
			else
				hdr->optslast[h]->next = opt;
			hdr->optslast[h] = opt;
		} else if (!empty(s)) {
			parse_error(lineno, "config file line not field nor header");
		}
		lineno++;
	}
	fclose(f);
	free(line);
	line = NULL;
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

static char *match_arg(char **av, char *arg)
{
	int len = strlen(arg);
	if (!strncmp(*av, arg, len)) {
		if ((*av)[len] == '=') {
			return len + 1 + *av;
		} else { 
			if (av[1] == NULL)
				usage();
			return av[1];
		}
	}
	return NULL;
}

/* Look for the config file argument before parsing the other 
   options because we want to read the config file first so 
   that command line options can conveniently override it. */
const char *config_file(char **av, const char *deffn)
{
	char *arg;
	while (*++av) { 
		if (!strcmp(*av, "--"))
			break;
		if ((arg = match_arg(av, "--conf")) != NULL)
			return arg;
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

int config_trigger(const char *header, const char *base, struct bucket_conf *bc)
{
	char *s;
	char *name;
	int n;

	xasprintf(&name, "%s-threshold", base);
	s = config_string(header, name);
	if (s) {
		if (bucket_conf_init(bc, s) < 0) {
			unparseable("trigger", header, name);
			return -1;
		}
	}
	free(name);
	name = NULL;

	xasprintf(&name, "%s-trigger", base);
	s = config_string(header, name);
	if (s) { 
		/* no $PATH */
		if (trigger_check(s) != 0) {
			SYSERRprintf("Trigger `%s' not executable\n", s);
			exit(1);
		}
		bc->trigger = s;
	}
	free(name);
	name = NULL;

	bc->log = 0;
	xasprintf(&name, "%s-log", base);
	n = config_bool(header, name);
	if (n >= 0)
		bc->log = n;
	free(name);
	name = NULL;

	return 0;
}

void config_cred(char *header, char *base, struct config_cred *cred)
{
	char *s;
	char *name;

	xasprintf(&name, "%s-user", base);
	if ((s = config_string(header, name)) != NULL) { 
		struct passwd *pw;
		if (!strcmp(s, "*"))
			cred->uid = -1U;
		else if ((pw = getpwnam(s)) == NULL)
			Eprintf("Unknown user `%s' in %s:%s config entry\n", 
				s, header, name);
		else
		        cred->uid = pw->pw_uid;
	}
	free(name);
	name = NULL;
	xasprintf(&name, "%s-group", base);	
	if ((s = config_string(header, name)) != NULL) { 
		struct group *gr;
		if (!strcmp(s, "*"))
			cred->gid = -1U;
		else if ((gr = getgrnam(s)) == NULL)
			Eprintf("Unknown group `%s' in %s:%s config entry\n", 
				header, name, s);
		else
			cred->gid = gr->gr_gid;
	}
	free(name);
	name = NULL;
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
		type = NULL;
		free(header);
		header = NULL;
		free(name);
		name = NULL;
	} 
	return 0;
}
#endif
