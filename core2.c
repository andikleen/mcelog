#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "mcelog.h"
#include "core2.h"

/* Decode P6 family (Core2) model specific errors. 
   The generic errors are decoded in p4.c */

/* [19..24] */
static char *bus_queue_req_type[] = {
	[0] = "BQ_DCU_READ_TYPE",
	[2] = "BQ_IFU_DEMAND_TYPE",
	[3] = "BQ_IFU_DEMAND_NC_TYPE",
	[4] = "BQ_DCU_RFO_TYPE",
	[5] = "BQ_DCU_RFO_LOCK_TYPE",
	[6] = "BQ_DCU_ITOM_TYPE",
	[8] = "BQ_DCU_WB_TYPE",
	[10] = "BC_DCU_WCEVICT_TYPE", 
	[11] = "BQ_DCU_WCLINE_TYPE",
	[12] = "BQ_DCU_BTM_TYPE",
	[13] = "BQ_DCU_INTACK_TYPE",
	[14] = "BQ_DCU_INVALL2_TYPE",
	[15] = "BQ_DCU_FLUSHL2_TYPE",
	[16] = "BQ_DCU_PART_RD_TYPE",
	[18] = "BQ_DCU_PART_WR_TYPE",
	[20] = "BQ_DCU_SPEC_CYC_TYPE",
	[24] = "BQ_DCU_IO_RD_TYPE",
	[25] = "BQ_DCU_IO_WR_TYPE",
	[28] = "BQ_DCU_LOCK_RD_TYPE",
	[30] = "BQ_DCU_SPLOCK_RD_TYPE",
	[29] = "BQ_DCU_LOCK_WR_TYPE",
};

/* [25..27] */
static char *bus_queue_error_type[] = {
	[0] = "BQ_ERR_HARD_TYPE",
	[1] = "BQ_ERR_DOUBLE_TYPE",
	[2] = "BQ_ERR_AERR2_TYPE",
	[4] = "BQ_ERR_SINGLE_TYPE",
	[5] = "BQ_ERR_AERR1_TYPE",
};

static char *reserved_3bits[8];
static char *reserved_1bit[2];
static char *reserved_2bits[4];

#define SINGLEBIT(n,d) static char *n[2] = { [1] = d }; 

SINGLEBIT(frc, "FRC error");
SINGLEBIT(berr, "BERR");
SINGLEBIT(int_binit, "internal BINIT");
SINGLEBIT(ext_binit, "external BINIT");
SINGLEBIT(response_parity, "response parity error");
SINGLEBIT(bus_binit, "bus BINIT");
SINGLEBIT(timeout_binit, "timeout BINIT (ROB timeout)");
SINGLEBIT(hard_err, "hard error");
SINGLEBIT(ierr, "IERR");
SINGLEBIT(aerr, "parity error");
SINGLEBIT(uecc, "uncorrectable ECC");
SINGLEBIT(cecc, "correctable ECC");

struct field {
	int start_bit;
	char **str;
	int stringlen;
};

#define FIELD(start_bit, name) { start_bit, name, NELE(name) }

struct field fields[] = { 
	FIELD(16, reserved_3bits),
	FIELD(19, bus_queue_req_type),
	FIELD(25, bus_queue_error_type),
	FIELD(25, bus_queue_error_type),
	FIELD(28, frc),
	FIELD(29, berr),
	FIELD(30, int_binit),
	FIELD(31, reserved_1bit),
	FIELD(32, reserved_3bits),
	FIELD(35, ext_binit),
	FIELD(36, response_parity),
	FIELD(37, bus_binit),
	FIELD(38, timeout_binit),
	FIELD(39, reserved_3bits),
	FIELD(42, hard_err),
	FIELD(43, ierr),
	FIELD(44, aerr),
	FIELD(45, uecc),
	FIELD(46, cecc),
	/* [47..54]: ECC syndrome */
	FIELD(55, reserved_2bits),
	{},
};

static u64 bitmask(u64 i)
{
	u64 mask = 1;
	while (mask < i) 
		mask = (mask << 1) | 1; 
	return mask;
}

void core2_decode_model(u64 status)
{
	struct field *f;
	int linelen = 0;
	char *delim = "";
	
	for (f = &fields[0]; f->str; f++) { 
		u64 v = (status >> f->start_bit) & bitmask(f->stringlen - 1);
		char *s = NULL;
		if (v < f->stringlen)
			s = f->str[v]; 
		if (!s) { 
			if (v == 0) 
				continue;
			char buf[60];
			s = buf; 
			snprintf(buf, sizeof buf, "<%u:%Lx>", f->start_bit, v);
		}
		int len = strlen(s);
		if (linelen + len > 75) {
			delim = "\n";
			linelen = 0;
		}
		Wprintf("%s%s", delim, s);
		delim = " ";
		linelen += len + 1; 
	}
	if (linelen > 0) 
		Wprintf("\n");
	if ((status >> 47) & 0xff)
		Wprintf("ECC syndrome: %Lx\n", (status >> 47) & 0xff);
}
