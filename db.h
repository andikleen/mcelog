#include <stdio.h>
struct database;
struct group;

struct database *open_db(char *fn, int wr);
int sync_db(struct database *db);
int close_db(struct database *db);
struct group *find_group(struct database *db, char *name);
char *entry_val(struct group *g, char *entry);
struct group *add_group(struct database *db, char *name, int *existed);
int delete_group(struct database *db, struct group *g);
void change_entry(struct database *db, struct group *g,
		  char *entry, char *newval);
void add_comment(struct database *db, struct group *group, char *comment);
struct group *first_group(struct database *db);
struct group *next_group(struct group *g);
void dump_group(struct group *g, FILE *out);
void dump_database(struct database *db, FILE *out);
struct group *find_entry(struct database *db, struct group *prev,
			 char *entry, char *value);
void rename_group(struct database *db, struct group *group, char *newname);
char *group_name(struct group *g);
unsigned long entry_num(struct group *g, char *entry);
void change_entry_num(struct database *db, struct group *g, char *entry,
		      unsigned long val);
void delete_entry(struct database *db, struct group *g, char *entry);
struct group *
clone_group(struct database *db, struct group *gold, char *newname);

