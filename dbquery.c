/* Access db files. This is for testing and debugging only. */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include "db.h"

#define C(x) if (x) printf(#x " failed: %s\n", strerror(errno))
#define NEEDGROUP  if (!group) { printf("need group first\n"); break; }

void Eprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void usage(void)
{
	printf(
	"s sync\n"
	"q close/quit\n"
	"ggroup find group\n"
	"G delete group\n"
	"agroup add group\n" 
	"ventry dump entry\n"
	"centry,val change entry to val\n"
	"fentry,val find entry with value and dump its group\n"
	"Ccomment add comment\n"
	"Lnewname clone group to newname\n"
	"d dump group\n"
	"D dump database\n");
}	

int main(int ac, char **av)
{
	struct database *db;
	struct group *group = NULL;
	char *line = NULL;
	size_t linesz = 0;
	if (!av[1]) {
		printf("%s database\n", av[0]);
		exit(1);
	}
	printf("dbtest\n");
	db = open_db(av[1], 1);  
	while (printf("> "), 
		fflush(stdout), 
		getline(&line, &linesz, stdin) > 0) { 
		char *p = line + strlen(line) - 1;
		while (p >= line && isspace(*p))
			*p-- = 0; 
		switch (line[0]) { 
		case 's':
			C(sync_db(db));
			break;
		case 'q':
			C(close_db(db)); 
			exit(0);
		case 'g':
			group = find_group(db, line + 1); 
			if (group)
				printf("found\n"); 
			break;
		case 'G':
			NEEDGROUP; 
			C(delete_group(db, group));
			group = NULL;
			break;			
		case 'a': { 
			int existed = 0;
			group = add_group(db, line + 1, &existed);
			if (existed) 
				printf("existed\n");
			break;
		}
		case 'v':
			NEEDGROUP; 
			printf("%s\n", entry_val(group, line + 1)); 
			break;
		case 'c': {
			p = line + 1;
			char *entry = strsep(&p, ","); 
			NEEDGROUP; 
			change_entry(db, group, entry, strsep(&p, ""));
			break;
		} 
		case 'L':
			NEEDGROUP; 
			clone_group(db, group, line + 1); 
			break;
		case 'f': { 
			struct group *g;
			p = line + 1; 
			char *entry = strsep(&p, ",");
			char *val = strsep(&p, "");	
			g = NULL;
			int nr = 0;
			while ((g = find_entry(db, g, entry, val)) != NULL) {
				if (nr == 0)
					group = g;
				nr++;
				dump_group(group, stdout);
			}
			if (nr == 0) 
				printf("not found\n");
			break;
		}
		case 'C':
			NEEDGROUP;
			add_comment(db, group, line + 1); 
			break;
		case 'd':
			NEEDGROUP;
			dump_group(group, stdout); 
			break;
		case 'D':
			dump_database(db, stdout);
			break;
		default:
			usage();
			break;
		}
	}
	return 0;
}
