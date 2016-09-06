
struct dmi_entry { 
	unsigned char type;
	unsigned char length;
	unsigned short handle;
} __attribute__((packed));

enum { 
	DMI_MEMORY_ARRAY = 16,
	DMI_MEMORY_DEVICE = 17,
	DMI_MEMORY_ARRAY_ADDR = 19,
	DMI_MEMORY_MAPPED_ADDR = 20,
};

struct dmi_memdev_addr {
	struct dmi_entry header;
	unsigned start_addr;
	unsigned end_addr;
	unsigned short dev_handle;
	unsigned short memarray_handle;
	unsigned char row;	
	unsigned char interleave_pos;
	unsigned char interleave_depth;
} __attribute__((packed)); 

struct dmi_memdev {
	struct dmi_entry header;
	unsigned short array_handle;
	unsigned short memerr_handle;
	unsigned short total_width;
	unsigned short data_width;
	unsigned short size;
	unsigned char form_factor;
	unsigned char device_set;
	unsigned char device_locator;
	unsigned char bank_locator;
	unsigned char memory_type;
	unsigned short type_details;
	unsigned short speed;
	unsigned char manufacturer;
	unsigned char serial_number;
	unsigned char asset_tag;
	unsigned char part_number;	
} __attribute__((packed));

struct dmi_memarray {
	struct dmi_entry header;
	unsigned char location;
	unsigned char use;
	unsigned char error_correction;
	unsigned int maximum_capacity;
	unsigned short error_handle;
	short num_devices;
} __attribute__((packed));

struct dmi_memarray_addr {
	struct dmi_entry header;
	unsigned int start_addr;
	unsigned int end_addr;
	unsigned short array_handle;
	unsigned partition_width;
}  __attribute__((packed));

int opendmi(void);
void dmi_decodeaddr(unsigned long long addr);
int dmi_sanity_check(void);
unsigned dmi_dimm_size(unsigned short size, char *unit);
struct dmi_memdev **dmi_find_addr(unsigned long long addr);
void dmi_set_verbosity(int v);

char *dmi_getstring(struct dmi_entry *e, unsigned number);
extern void checkdmi(void);
void closedmi(void);

/* valid after opendmi: */
extern struct dmi_memdev **dmi_dimms; 
extern struct dmi_memdev_addr **dmi_ranges; 
extern struct dmi_memarray **dmi_arrays; 
extern struct dmi_memarray_addr **dmi_array_ranges; 

extern int dmi_forced;
extern int do_dmi;
