/* High level interface to disk based DIMM database */
/* Note: obsolete: new design is in memdb.c */
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include "mcelog.h"
#include "diskdb.h"
#include "paths.h"
#include "dimm.h"
#include "dmi.h"
 
char *error_trigger;
unsigned error_thresh = 20;
char *dimm_db_fn = DIMM_DB_FILENAME;

static void checkdimmdb(void)
{
	if (open_dimm_db(dimm_db_fn) < 0) 
		exit(1);
}

int diskdb_modifier(int opt)
{
	char *end;

	switch (opt) { 
	case O_DATABASE:
		dimm_db_fn = optarg;
		checkdmi();
		checkdimmdb();
		break;
	case O_ERROR_TRIGGER:
		checkdmi();
		open_dimm_db(dimm_db_fn);
		error_thresh = strtoul(optarg, &end, 0);
		if (end == optarg || *end != ',') 
			usage();
		error_trigger = end + 1; 
		break;
	default:
		return 0;
	}
	return 1;
}

void diskdb_resolve_addr(u64 addr)
{
	if (open_dimm_db(dimm_db_fn) >= 0) 
		new_error(addr, error_thresh, error_trigger);
}


void diskdb_usage(void)
{
	fprintf(stderr, 
		"Manage disk DIMM error database\n"
		"  mcelog [options] --drop-old-memory|--reset-memory locator\n"
		"  mcelog --dump-memory locator\n"
		"  old can be either locator or name\n"
		"Disk database options:"
		"--database fn       Set filename of DIMM database (default " DIMM_DB_FILENAME ")\n"
		"--error-trigger cmd,thresh   Run cmd on exceeding thresh errors per DIMM\n");
}


static void dimm_common(int ac, char **av)
{
	no_syslog();
	checkdmi();
	checkdimmdb();
	argsleft(ac, av); 
}

int diskdb_cmd(int opt, int ac, char **av)
{
	char *arg = optarg; 
	
	switch (opt) { 
	case O_DUMP_MEMORY:
		dimm_common(ac, av);
		if (arg)
			dump_dimm(arg);
		else
			dump_all_dimms();
		return 1;
	case O_RESET_MEMORY:
		dimm_common(ac, av);
		reset_dimm(arg);
		return 1;
	case O_DROP_OLD_MEMORY:
		dimm_common(ac, av);
		gc_dimms();
		return 1;
	}
	return 0;	
}
