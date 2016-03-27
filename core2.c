#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "mcelog.h"
#include "core2.h"
#include "bitfield.h"

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

static struct field p6_shared_status[] = { 
	FIELD(16, reserved_3bits),
	FIELD(19, bus_queue_req_type),
	FIELD(25, bus_queue_error_type),
	FIELD(25, bus_queue_error_type),
	SBITFIELD(30, "internal BINIT"),
	SBITFIELD(36, "received parity error on response transaction"),
	SBITFIELD(38, "timeout BINIT (ROB timeout)."
		  " No micro-instruction retired for some time"),
	FIELD(39, reserved_3bits),
	SBITFIELD(42, "bus transaction received hard error response"),
	SBITFIELD(43, "failure that caused IERR"),
	/* The following are reserved for Core in the SDM. Let's keep them here anyways*/
	SBITFIELD(44, "two failing bus transactions with address parity error (AERR)"),
	SBITFIELD(45, "uncorrectable ECC error"),
	SBITFIELD(46, "correctable ECC error"),
	/* [47..54]: ECC syndrome */
	FIELD(55, reserved_2bits),
	{},
};

static struct field p6old_status[] = { 
	SBITFIELD(28, "FRC error"),
	SBITFIELD(29, "BERR on this CPU"),
	FIELD(31, reserved_1bit),
	FIELD(32, reserved_3bits),
	SBITFIELD(35, "BINIT received from external bus"),
	SBITFIELD(37, "Received hard error response on split transaction (Bus BINIT)"),
	{}
};

static struct field core2_status[] = {
	SBITFIELD(28, "MCE driven"),
	SBITFIELD(29, "MCE is observed"),
	SBITFIELD(31, "BINIT observed"),
	FIELD(32, reserved_2bits),
	SBITFIELD(34, "PIC or FSB data parity error"),
	FIELD(35, reserved_1bit),
	SBITFIELD(37, "FSB address parity error detected"),
	{}
};

static struct numfield p6old_status_numbers[] = { 
	HEXNUMBER(47, 54, "ECC syndrome"),
	{}
};

void core2_decode_model(u64 status)
{	
	decode_bitfield(status, p6_shared_status);
	decode_bitfield(status, core2_status);
	/* Normally reserved, but let's parse anyways: */
	decode_numfield(status, p6old_status_numbers);
}

void p6old_decode_model(u64 status)
{
	decode_bitfield(status, p6_shared_status);
	decode_bitfield(status, p6old_status);	
	decode_numfield(status, p6old_status_numbers);
}
