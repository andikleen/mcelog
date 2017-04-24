/* Copyright (C) 2006 Andi Kleen, SuSE Labs.
   Portions Copyright (C) 2016 Sergio Gelato.

   Use SMBIOS/DMI to map address to DIMM description.
   For reference see the SMBIOS specification 2.4, 3.0

   dmi is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version
   2.

   dmi is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should find a copy of v2 of the GNU General Public License somewhere
   on your Linux system; if not, write to the Free Software Foundation, 
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
/* Notebook
   add an option to dump existing errors in SMBIOS?
   implement code to look up PCI resources too.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/mman.h>

#include "mcelog.h"
#include "dmi.h"
#include "memutil.h"

static int verbose = 0;
int dmi_forced;
int do_dmi;

struct anchor { 
	char str[4];	/* _SM_ */
	char csum;
	char entry_length;
	char major;
	char minor;
	short maxlength;
	char rev;
	char fmt[5];
	char str2[5]; /* _DMI_ */
	char csum2;
	unsigned short length;
	unsigned table;
	unsigned short numentries;
	char bcdrev;
} __attribute__((packed));

static struct dmi_entry *entries;
static size_t entrieslen;
static int numentries;
static size_t dmi_length;
static struct dmi_entry **handle_to_entry;

struct dmi_memdev **dmi_dimms; 
struct dmi_memarray **dmi_arrays;
struct dmi_memdev_addr **dmi_ranges; 
struct dmi_memarray_addr **dmi_array_ranges; 

static void collect_dmi_dimms(void);
static struct dmi_entry **dmi_collect(int type, int minsize, int *len);
static void dump_ranges(struct dmi_memdev_addr **, struct dmi_memdev **);

static unsigned checksum(unsigned char *s, int len)
{
	unsigned char csum = 0;
	int i;
	for (i = 0; i < len; i++)
		csum += s[i];
	return csum;
}

/* Check if entry is valid */
static int check_entry(struct dmi_entry *e, struct dmi_entry **next) 
{
	char *end = (char *)entries + dmi_length;
	char *s;
	if (!e)
		return 0;
 	s = (char *)e + e->length;
	if (verbose > 3)
		printf("length %d handle %x\n", e->length, e->handle);
	do {
		if (verbose > 3) 
			printf("string %s\n", s);
		while (s < end-1 && *s)
			s++;
		if (s >= end-1) { 
			if (verbose > 0) 
				printf("handle %x length %d truncated\n",
					e->handle, e->length);
			return 0;
		}
		s++;
	} while (*s);
	if (s >= end) 
		*next = NULL;
	else
		*next = (struct dmi_entry *)(s + 1);
	return 1;
}

/* Relies on sanity checks in check_entry */
char *dmi_getstring(struct dmi_entry *e, unsigned number)
{
	char *s = (char *)e + e->length;
	if (number == 0) 
		return "";
	do { 
		if (--number == 0) 
			return s;
		while (*s)
			s++;
		s++;
	} while (*s);
	return NULL;
}

static void fill_handles(void)
{
	int i;
	struct dmi_entry *e, *next;
	e = entries;
	handle_to_entry = xalloc(sizeof(void *) * 0xffff);
	for (i = 0; i < numentries; i++, e = next) { 
		if (!check_entry(e, &next))
			break;
		handle_to_entry[e->handle] = e; 
	}
}

static int append_sysfs_dmi_entry(unsigned char type, int instance)
{
	char filename[64];	/* 40 bytes should be enough */
	char buf[1024];
	int r;
	ssize_t nr;
	size_t l;
	int fd;
	r = snprintf(filename, sizeof(filename),
		     "/sys/firmware/dmi/entries/%hhu-%d/raw",
		     type, instance);
	if (r < 0 || (unsigned int)r >= sizeof(filename)) {
		Eprintf("Can't build pathname for DMI type %hhu instance %d\n",
			type, instance);
		return 0;
	}
	fd = open(filename, O_RDONLY);
	if (fd == (-1)) {
		if (errno != ENOENT)
			perror(filename);
		return 0;
	}
	l = dmi_length;
	for (;;) {
		nr = read(fd, buf, sizeof(buf));
		if (nr < 0) {
			if (errno == EINTR)
				continue;
			perror(filename);
			close(fd);
			return 0;
		} else if (nr > 0) {
			while (l + nr > entrieslen) {
				entrieslen += 4096;
				entries = xrealloc(entries, entrieslen);
			}
			memcpy((char *)entries+l, buf, nr);
			l += nr;
		} else {
			numentries ++;
			dmi_length = l;
			close(fd);
			return 1;
		}
	}
}

static void append_sysfs_dmi_entries(unsigned char type)
{
	int i;
	for (i=0; append_sysfs_dmi_entry(type, i); i++) ;
}

static int get_efi_base_addr(size_t *address)
{
	FILE *efi_systab;
	const char *filename;
	char linebuf[64];
	int ret = 0;

	*address = 0; /* Prevent compiler warning */

	/* Linux 2.6.7 and up: /sys/firmware/efi/systab */
	filename = "/sys/firmware/efi/systab";
	if ((efi_systab = fopen(filename, "r")) != NULL)
		goto check_symbol;

	/* Linux up to 2.6.6: /proc/efi/systab */
	filename = "/proc/efi/systab";
	if ((efi_systab = fopen(filename, "r")) != NULL)
		goto check_symbol;

	/* Failed to open EFI interfaces */
	return ret;

check_symbol:
	while ((fgets(linebuf, sizeof(linebuf) - 1, efi_systab)) != NULL) {
		char *addrp = strchr(linebuf, '=');
		if (!addrp)
			break;
		*(addrp++) = '\0';

		if (strcmp(linebuf, "SMBIOS") == 0) {
			*address = strtoul(addrp, NULL, 0);
			ret = 1;
			break;
		}
	}

	if (fclose(efi_systab) != 0)
		perror(filename);

	if (!ret || !*address){
		Lprintf("No valid SMBIOS entry point: Continue without DMI decoding");
		return 0;
	}

	if (verbose)
		printf("%s: SMBIOS entry point at 0x%08lx\n", filename,
			(unsigned long)*address);
	return ret;
}

int opendmi(void)
{
	struct anchor *a, *abase;
	void *ebase;
	void *p, *q;
	int pagesize = getpagesize();
	int memfd; 
	off_t emapbase, corr;
	size_t emapsize;
	int err = -1;
	const int segsize = 0x10000;
	size_t entry_point_addr = 0;
	size_t length = 0;

	if (entries)
		return 0;

	if (access("/sys/firmware/dmi/entries/0-0/raw", R_OK) == 0) {
		numentries = 0;
		append_sysfs_dmi_entries(DMI_MEMORY_ARRAY);
		append_sysfs_dmi_entries(DMI_MEMORY_DEVICE);
		append_sysfs_dmi_entries(DMI_MEMORY_ARRAY_ADDR);
		append_sysfs_dmi_entries(DMI_MEMORY_MAPPED_ADDR);
		fill_handles();
		collect_dmi_dimms();
		return 0;
	}

	memfd = open("/dev/mem", O_RDONLY);
	if (memfd < 0) { 
		Eprintf("Cannot open /dev/mem for DMI decoding: %s",
			strerror(errno));
		return -1;
	}	

	/*
	 * On EFI-based systems, the SMBIOS Entry Point structure can be
	 * located by looking in the EFI Configuration Table.
	 */
	if (get_efi_base_addr(&entry_point_addr)) {
		size_t addr_start = round_down(entry_point_addr, pagesize);
		size_t addr_end = round_up(entry_point_addr + 0x20, pagesize);
		length = addr_end - addr_start;

		/* mmap() the address of SMBIOS structure table entry point. */
		abase = mmap(NULL, length, PROT_READ, MAP_SHARED, memfd,
					addr_start);
		if (abase == (struct anchor *)-1) {
			Eprintf("Cannot mmap 0x%lx for efi mode: %s",
				(unsigned long)entry_point_addr,
				strerror(errno));
			goto legacy;
		}
		a = (struct anchor*)((char*)abase + (entry_point_addr - addr_start));
		goto fill_entries;
	}

legacy:
	/*
	 * On non-EFI systems, the SMBIOS Entry Point structure can be located
	 * by searching for the anchor-string on paragraph (16-byte) boundaries
	 * within the physical memory address range 000F0000h to 000FFFFFh
	 */
	length = segsize - 1;
	abase = mmap(NULL, length, PROT_READ, MAP_SHARED, memfd, 0xf0000);

	if (abase == (struct anchor *)-1) {
		Eprintf("Cannot mmap 0xf0000 for legacy mode: %s",
			strerror(errno));
		goto out;
	}   

	for (p = abase, q = p + segsize - 1; p < q; p += 0x10) {
		if (!memcmp(p, "_SM_", 4) && 
		    (checksum(p, ((struct anchor *)p)->entry_length) == 0)) 
			break;
	}

	if (p >= q) {
		Eprintf("Cannot find SMBIOS DMI tables");
		goto out_mmap;
	}

	a = p;

fill_entries:
	if (verbose) 
		printf("DMI tables at %x, %u bytes, %u entries\n", 
			a->table, a->length, a->numentries);
	emapbase = round_down(a->table, pagesize);
	corr = a->table - emapbase;
	emapsize = round_up(a->table + a->length, pagesize) - emapbase;
	ebase = mmap(NULL, emapsize, PROT_READ, MAP_SHARED, memfd, emapbase);
	if (ebase == MAP_FAILED) {
		Eprintf("Cannot mmap SMBIOS tables at %x", a->table);
		goto out_mmap;
	}
	entrieslen = a->length;
	entries = xalloc_nonzero(entrieslen);
	memcpy(entries, (char *)ebase+corr, entrieslen);
	munmap(ebase, emapsize);
	numentries = a->numentries;
	dmi_length = a->length;
	fill_handles();
	collect_dmi_dimms(); 
	err = 0;

out_mmap:
	munmap(abase, length);
out:
	close(memfd);
	return err;	
}

unsigned dmi_dimm_size(unsigned short size, char *unit)
{
	unsigned mbflag = !(size & (1<<15));
	size &= ~(1<<15);
	strcpy(unit, "KB");
	if (mbflag) {
		unit[0] = 'M';
		if (size >= 1024) {
			unit[0] = 'G';
			size /= 1024;
		}
	}
	return size;
}

static char *form_factors[] = { 
	"?",
	"Other", "Unknown", "SIMM", "SIP", "Chip", "DIP", "ZIP", 
	"Proprietary Card", "DIMM", "TSOP", "Row of chips", "RIMM",
	"SODIMM", "SRIMM", "FB-DIMM"
};
static char *memory_types[] = {
	"?",
	"Other", "Unknown", "DRAM", "EDRAM", "VRAM", "SRAM", "RAM",
	"ROM", "FLASH", "EEPROM", "FEPROM", "EPROM", "CDRAM", "3DRAM",
	"SDRAM", "SGRAM", "RDRAM", "DDR", "DDR2", "DDR2 FB-DIMM",
	"Reserved 0x15", "Reserved 0x16", "Reserved 0x17", "DDR3",
	"FBD2", "DDR4", "LPDDR", "LPDDR2", "LPDDR3", "LPDDR4"
};

#define LOOKUP(array, val, buf) \
	((val) >= NELE(array) ? \
	 (sprintf(buf,"<%u>",(val)), (buf)) : \
	 (array)[val])

static char *type_details[16] = {
	"Reserved", "Other", "Unknown", "Fast-paged", "Static Column",
	"Pseudo static", "RAMBUS", "Synchronous", "CMOS", "EDO",
	"Window DRAM", "Cache DRAM", "Non-volatile", "Registered",
	"Unbuffered", "LRDIMM"
}; 

static void dump_type_details(unsigned short td)
{
	int i;
	if (!td)
		return;
	for (i = 0; i < 16; i++) 
		if (td & (1<<i))
			Wprintf("%s ", type_details[i]);
}

static void dump_memdev(struct dmi_memdev *md, unsigned long long addr)
{
	char tmp[20];
	char unit[10];
	char *s;

	if (md->header.length < 
			offsetof(struct dmi_memdev, manufacturer)) { 
		if (verbose > 0)
			printf("Memory device for address %llx too short %u\n",
			       addr, md->header.length);
		return;
	}	

	Wprintf("%s ", LOOKUP(memory_types, md->memory_type, tmp));
	if (md->form_factor >= 3) 
		Wprintf("%s ", LOOKUP(form_factors, md->form_factor, tmp));
	if (md->speed != 0)
		Wprintf("%hu Mhz ", md->speed);
	dump_type_details(md->type_details);
	Wprintf("Width %hu Data Width %hu Size %u %s\n",
		md->total_width, md->data_width, 
		dmi_dimm_size(md->size, unit), unit);

#define DUMPSTR(n,x) \
	if (md->x) { \
		s = dmi_getstring(&md->header, md->x);	\
		if (s && *s && strcmp(s,"None"))	\
			Wprintf(n ": %s\n", s);		\
	}
	DUMPSTR("Device Locator", device_locator);
	DUMPSTR("Bank Locator", bank_locator);
	if (md->header.length < offsetof(struct dmi_memdev, manufacturer))
		return;
	DUMPSTR("Manufacturer", manufacturer);
	DUMPSTR("Serial Number", serial_number);
	DUMPSTR("Asset Tag", asset_tag);
	DUMPSTR("Part Number", part_number);
}

static void warnuser(void)
{
	static int warned; 
	if (warned) 
		return;
	warned = 1;
	Wprintf("WARNING: "
	"SMBIOS data is often unreliable. Take with a grain of salt!\n");
}

static int cmp_range(const void *a, const void *b)
{
	struct dmi_memdev_addr *ap = *(struct dmi_memdev_addr **)a; 
	struct dmi_memdev_addr *bp = *(struct dmi_memdev_addr **)b;
	return (int)ap->start_addr - (int)bp->end_addr;
}

static int cmp_arr_range(const void *a, const void *b)
{
	struct dmi_memarray_addr *ap = *(struct dmi_memarray_addr **)a; 
	struct dmi_memarray_addr *bp = *(struct dmi_memarray_addr **)b;
	return (int)ap->start_addr - (int)bp->end_addr;
}

#define COLLECT(var, id, ele) {						  \
	typedef typeof (**(var)) T;					  \
	var = (T **)dmi_collect(id,					  \
		        offsetof(T, ele) + sizeof_field(T, ele),  	  \
			&len);						  \
}

static void collect_dmi_dimms(void)
{
	int len; 
	
	COLLECT(dmi_ranges, DMI_MEMORY_MAPPED_ADDR, dev_handle);
	qsort(dmi_ranges, len, sizeof(struct dmi_entry *), cmp_range);
	COLLECT(dmi_dimms, DMI_MEMORY_DEVICE, device_locator);
	if (verbose > 1)
		dump_ranges(dmi_ranges, dmi_dimms);
	COLLECT(dmi_arrays, DMI_MEMORY_ARRAY, location);
	COLLECT(dmi_array_ranges, DMI_MEMORY_ARRAY_ADDR, array_handle); 
	qsort(dmi_array_ranges, len, sizeof(struct dmi_entry *),cmp_arr_range);
}

#undef COLLECT

static struct dmi_entry **
dmi_collect(int type, int minsize, int *len)
{
	struct dmi_entry **r; 
	struct dmi_entry *e, *next;
	int i, k;
	r = xalloc(sizeof(struct dmi_entry *) * (numentries + 1));
	k = 0;
	e = entries;
	next = NULL;
	for (i = 0; i < numentries; i++, e = next) { 
		if (!check_entry(e, &next))
			break; 
		if (e->type != type)
			continue;
		if (e->length < minsize) { 
			if (verbose > 0) 
				printf("hnd %x size %d expected %d\n",
				       e->handle, e->length, minsize);
			continue;
		}		
		if (type == DMI_MEMORY_DEVICE && 
		    ((struct dmi_memdev *)e)->size == 0) { 
			if (verbose > 0) 
				printf("entry %x disabled\n", e->handle);
			continue;
		}
		r[k++] = e; 
	}
	*len = k;
	return r;
} 

#define FAILED " SMBIOS DIMM sanity check failed\n"

int dmi_sanity_check(void)
{
	int i, k;
	int numdmi_dimms = 0;
	int numranges = 0;

	if (dmi_ranges[0] == NULL)
		return 0;

	for (k = 0; dmi_dimms[k]; k++)
		numdmi_dimms++;

	/* Do we have multiple ranges? */
	for (k = 1; dmi_ranges[k]; k++) {
		if (dmi_ranges[k]->start_addr <= dmi_ranges[k-1]->end_addr) { 
			return 0;
		}
		if (dmi_ranges[k]->start_addr >= dmi_ranges[k-1]->end_addr)
			numranges++;
	}
	if (numranges == 1 && numdmi_dimms > 2) {
		if (verbose > 0)
			printf("Not enough unique address ranges." FAILED); 
		return 0;
	}

	/* Unique locators? */
	for (k = 0; dmi_dimms[k]; k++) {
		char *loc;
		loc  = dmi_getstring(&dmi_dimms[k]->header,
				     dmi_dimms[k]->device_locator);
		if (!loc) {
			if (verbose > 0)
				printf("Missing locator." FAILED);
			return 0; 
		}
		for (i = 0; i < k; i++) {
			char *b = dmi_getstring(&dmi_dimms[i]->header,
						dmi_dimms[i]->device_locator);
			if (!b)
				continue;
			if (!strcmp(b, loc)) {
				if (verbose > 0)
					printf("Ambiguous locators `%s'<->`%s'."
					       FAILED, b, loc);
				return 0;
			}
		}
	}
				
	return 1;
}

#define DMIGET(p, member) \
(offsetof(typeof(*(p)), member) + sizeof((p)->member) <= (p)->header.length ? \
	(p)->member : 0)

static void 
dump_ranges(struct dmi_memdev_addr **ranges, struct dmi_memdev **dmi_dimms)
{
	int i;
	printf("RANGES\n");
	for (i = 0; ranges[i]; i++) 
		printf("range %x-%x h %x a %x row %u ilpos %u ildepth %u\n",
			ranges[i]->start_addr,
			ranges[i]->end_addr,
			ranges[i]->dev_handle,
			DMIGET(ranges[i], memarray_handle),
			DMIGET(ranges[i], row),
			DMIGET(ranges[i], interleave_pos),
			DMIGET(ranges[i], interleave_depth));
	printf("DMI_DIMMS\n");
	for (i = 0; dmi_dimms[i]; i++) 
		printf("dimm h %x width %u datawidth %u size %u set %u\n", 
			dmi_dimms[i]->header.handle,
			dmi_dimms[i]->total_width,
			DMIGET(dmi_dimms[i],data_width),
			DMIGET(dmi_dimms[i],size),
			DMIGET(dmi_dimms[i],device_set));
}

struct dmi_memdev **dmi_find_addr(unsigned long long addr)
{
	struct dmi_memdev **devs; 
	int i, k;
	
	devs = xalloc(sizeof(void *) * (numentries+1));
	k = 0;
	for (i = 0; dmi_ranges[i]; i++) { 
		struct dmi_memdev_addr *da = dmi_ranges[i];
		if (addr < ((unsigned long long)da->start_addr)*1024 ||
		    addr >= ((unsigned long long)da->end_addr)*1024)
			continue;
		devs[k] = (struct dmi_memdev *)handle_to_entry[da->dev_handle];
		if (devs[k]) 
			k++;
	} 

#if 0
	/* Need to implement proper decoding of interleaving sets before
	   enabling this. */
	int j, w;
	for (i = 0; dmi_array_ranges[i]; i++) { 
		struct dmi_memarray_addr *d = dmi_array_ranges[i];
		if (addr < ((unsigned long long)d->start_addr)*1024 ||
		    addr >= ((unsigned long long)d->end_addr)*1024)
			continue;
		for (w = 0; dmi_dimms[w]; w++) {
			struct dmi_memdev *m = dmi_dimms[w];
			if (m->array_handle == d->array_handle) {
				for (j = 0; j < k; j++) {
					if (devs[j] == m)
						break;
				}
				if (j == k)
					devs[k++] = m;
			}
		}
	} 
#endif

	devs[k] = NULL;
	return devs;
}

void dmi_decodeaddr(unsigned long long addr)
{
	struct dmi_memdev **devs = dmi_find_addr(addr);
	if (devs[0]) { 
		int i;
		warnuser();
		for (i = 0; devs[i]; i++) 
			dump_memdev(devs[i], addr);
	} else { 
		Wprintf("No DIMM found for %llx in SMBIOS\n", addr);
	}
	free(devs);
} 

void dmi_set_verbosity(int v)
{
	verbose = v;
}

void checkdmi(void)
{
	static int dmi_checked;
	if (dmi_checked)
		return;
	dmi_checked = 1;
	if (dmi_forced && !do_dmi)
		return;
	if (opendmi() < 0) {
		if (dmi_forced)
			exit(1);
		do_dmi = 0;
		return; 
	}
	if (!dmi_forced)
		do_dmi = dmi_sanity_check();
}

#define FREE(x) free(x), (x) = NULL

void closedmi(void)
{
	if (!entries) 
		return;
	FREE(dmi_dimms);
	FREE(dmi_arrays);
	FREE(dmi_ranges);
	FREE(dmi_array_ranges);
	FREE(handle_to_entry);
	FREE(entries);
	entrieslen = 0;
}
