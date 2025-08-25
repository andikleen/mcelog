/* Copyright (C) 2022 Intel Corporation
   Decode Intel Granite Rapids specific machine check errors.

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

   Author: Tony Luck
*/

#include "mcelog.h"
#include "bitfield.h"
#include "granite.h"
#include "memdb.h"

static char *upi_2[] = {
	[0x00] = "UC Phy Initialization Failure (NumInit)",
	[0x01] = "UC Phy Detected Drift Buffer Alarm",
	[0x02] = "UC Phy Detected Latency Buffer Rollover",
	[0x10] = "UC LL Rx detected CRC error: unsuccessful LLR (entered Abort state)",
	[0x11] = "UC LL Rx Unsupported/Undefined packet",
	[0x12] = "UC LL or Phy Control Error",
	[0x13] = "UC LL Rx Parameter Exception",
	[0x14] = "UC LL TDX Failure",
	[0x15] = "UC LL SGX Failure",
	[0x16] = "UC LL Tx SDC Parity Error",
	[0x17] = "UC LL Rx SDC Parity Error",
	[0x18] = "UC LL FLE Failure",
	[0x1F] = "UC LL Detected Control Error",
	[0x20] = "COR Phy Initialization Abort",
	[0x21] = "COR Phy Inband Reset",
	[0x22] = "COR Phy Lane failure, recovery in x8 width",
	[0x23] = "COR Phy L0c error corrected without Phy reset",
	[0x24] = "COR Phy L0c error triggering Phy reset",
	[0x25] = "COR Phy L0p exit error corrected with reset",
	[0x30] = "COR LL Rx detected CRC error: successful LLR without Phy Reinit",
	[0x31] = "COR LL Rx detected CRC error: successful LLR with Phy Reinit",
};

static struct field upi2[] = {
	FIELD(0, upi_2),
	{}
};

static char *punit_errs_1[] = {
	[0x02 ... 0x3] = "Power Management Unit microcontroller double-bit ECC error",
	[0x08 ... 0x9] = "Power Management Unit microcontroller error",
	[0x0a] = "Power Management Unit microcontroller patch load error",
	[0x0b] = "Power Management Unit microcontroller POReqValid error",
	[0x10] = "Power Management Unit microcontroller TeleSRAM double-bit ECC error",
	[0x20] = "Power Management Agent signaled error",
	[0x80] = "S3M signaled error",
	[0xa0] = "Power Management Unit HPMSRAM double-bit ECC error detected",
	[0xb0] = "Power Management Unit TPMISRAM double-bit ECC error detected",
};

static struct field punit1[] = {
	FIELD(0, punit_errs_1),
	{}
};

static char *punit_errs_2[] = {
	[0x09] = "MCA_TSC_DOWNLOAD_TIMEOUT: TSC download timed out",
	[0x0b] = "MCA_GPSB_TIMEOUT: GPSB does not respond within timeout value",
	[0x0c] = "MCA_PMSB_TIMEOUT: PMSB does not respond within timeout value",
	[0x10] = "MCA_PMAX_CALIB_ERROR: PMAX calibration error",
	[0x1a] = "MCA_DISP_RUN_BUSY_TIMEOUT: Dispatcher busy beyond timeout",
	[0x1d] = "MCA_MORE_THAN_ONE_LT_AGENT: During Boot Mode Processing, >1 LT Agent detected",
	[0x23] = "MCA_PCU_SVID_ERROR: SVID error",
	[0x35] = "MCA_SVID_LOADLINE_INVALID: Invalid SVID VCCIN Loadline resistance value",
	[0x36] = "MCA_SVID_ICCMAX_INVALID: Invalid SVID ICCMAX value",
	[0x40] = "MCA_SVID_VIDMAX_INVALID: Invalid SVID VID_MAX value",
	[0x41] = "MCA_SVID_VDDRAMP_INVALID: Invalid ramp voltage for VDD_RAMP",
	[0x48] = "MCA_ITD_FUSE_INVALID: ITD fuse settings are not valid",
	[0x49] = "MCA_SVID_DC_LL_INVALID: ITD fuse settings are not valid",
	[0x4a] = "MCA_FIVR_PD_HARDERR: PM event has issued some FIVR Fault",
	[0x4c] = "MCA_HPM_DOUBLE_BIT_ERROR_DETECTED: Read from HPM SRAM resulted in Double bit error",
	[0x56] = "SVID_ACTIVE_VID_FUSE_ERROR: Invaid fuse values programmed for SVID Active vid",
};

static struct field punit2[] = {
	FIELD(0, punit_errs_2),
	{}
};

static char *b2cmi_1[] = {
	[0x01] = "Read ECC error",
	[0x02] = "Bucket1 error",
	[0x03] = "Tracker Parity error",
	[0x04] = "Security mismatch",
	[0x07] = "Read completion parity error",
	[0x08] = "Response parity error",
	[0x09] = "Timeout error",
	[0x0a] = "Address parity error",
	[0x0c] = "CMI credit over subscription error",
	[0x0d] = "SAI mismatch error",
};

static struct field b2cmi1[] = {
	FIELD(0, b2cmi_1),
	{}
};

static char *mcchan_0[] = {
	[0x01] = "Address Parity Error (APPP)",
	[0x02] = "CMI Wr data parity Error on sCH0",
	[0x03] = "CMI Uncorr/Corr ECC error on sCH0",
	[0x04] = "CMI Wr BE parity Error on sCH0",
	[0x05] = "CMI Wr MAC parity Error on sCH0",
	[0x08] = "Corr Patrol Scrub Error",
	[0x10] = "UnCorr Patrol Scrub Error",
	[0x20] = "Corr Spare Error",
	[0x40] = "UnCorr Spare Error",
	[0x80] = "Transient or Correctable Error for Demand or Underfill Reads",
	[0xa0] = "Uncorrectable Error for Demand or Underfill Reads",
	[0xb0] = "Poison was read from memory when poison was disabled in memory controller",
	[0xc0] = "Read 2LM MetaData Error",
};

static char *mcchan_1[] = {
	[0x00] = "WDB Read Parity Error on sCH0",
	[0x02] = "WDB Read Uncorr/Corr ECC Error on sCH0",
	[0x04] = "WDB BE Read Parity Error on sCH0",
	[0x06] = "WDB Read Persistent Corr ECC Error on sCH0",
	[0x08] = "DDR Link Fail",
	[0x09] = "Illegal incoming opcode",
};

static char *mcchan_2[] = {
	[0x00] = "DDR CAParity or WrCRC Error",
};

static char *mcchan_4[] = {
	[0x00] = "Scheduler address parity error",
};

static char *mcchan_8[] = {
	[0x32] = "MC Internal Errors",
	[0x33] = "MCTracker Address RF parity error",
};

static char *mcchan_32[] = {
	[0x02] = "CMI Wr data parity Error on sCH1",
	[0x03] = "CMI Uncorr/Corr ECC error on sCH1",
	[0x04] = "CMI Wr BE parity Error on sCH1",
	[0x05] = "CMI Wr MAC parity Error on sCH1",
};

static char *mcchan_33[] = {
	[0x00] = "WDB Read Parity Error on sCH1",
	[0x02] = "WDB Read Uncorr/Corr ECC Error on sCH1",
	[0x04] = "WDB BE Read Parity Error on sCH1",
	[0x06] = "WDB Read Persistent Corr ECC Error on sCH1",
};

static struct field mcchan0[] = {
	FIELD(0, mcchan_0),
	{}
};

static struct field mcchan1[] = {
	FIELD(0, mcchan_1),
	{}
};

static struct field mcchan2[] = {
	FIELD(0, mcchan_2),
	{}
};

static struct field mcchan4[] = {
	FIELD(0, mcchan_4),
	{}
};

static struct field mcchan8[] = {
	FIELD(0, mcchan_8),
	{}
};

static struct field mcchan32[] = {
	FIELD(0, mcchan_32),
	{}
};

static struct field mcchan33[] = {
	FIELD(0, mcchan_33),
	{}
};

static void granite_imc_misc(u64 status, u64 misc)
{
	u64 mscod = EXTRACT(status, 16, 31);
	u32 column = EXTRACT(misc, 9, 18) << 2;
	u32 row = EXTRACT(misc, 19, 36);
	u32 bank = EXTRACT(misc, 37, 38);
	u32 bankgroup = EXTRACT(misc, 39, 41);
	u32 fdevice = EXTRACT(misc, 43, 48);
	u32 subrank = EXTRACT(misc, 51, 54);
	u32 chipselect = EXTRACT(misc, 55, 57);
	u32 eccmode = EXTRACT(misc, 58, 61);
	u32 transient = EXTRACT(misc, 63, 63);

	if (mscod >= 0x800 && mscod <= 0x82f)
		return;

	Wprintf("bank: 0x%x bankgroup: 0x%x row: 0x%x column: 0x%x\n", bank, bankgroup, row, column);
	if (!transient)
		Wprintf("failed device: 0x%x\n", fdevice);
	Wprintf("chipselect: 0x%x subrank: 0x%x\n", chipselect, subrank);
	Wprintf("ecc mode: ");
	switch (eccmode) {
	case 1: Wprintf("SDDC 128b 1LM\n"); break;
	case 2: Wprintf("SDDC 125b 1LM\n"); break;
	case 3: Wprintf("SDDC 96b 1LM\n"); break;
	case 4: Wprintf("SDDC 96b 2LM\n"); break;
	case 5: Wprintf("ADDDC 80b 1LM\n"); break;
	case 6: Wprintf("ADDDC 80b 2LM\n"); break;
	case 7: Wprintf("ADDDC 64b 1LM\n"); break;
	case 8: Wprintf("9x4 61b 1LM\n"); break;
	case 9: Wprintf("9x4 32b 1LM\n"); break;
	case 10: Wprintf("9x4 32b 2LM\n"); break;
	default: Wprintf("Invalid/unknown ECC mode\n"); break;
	}
	if (transient)
		Wprintf("transient\n");
}

void granite_decode_model(int cputype, int bank, u64 status, u64 misc)
{
	u64 f;

	switch (bank) {
	case 5: /* UPI */
		Wprintf("UPI: ");
		f = EXTRACT(status, 16, 31);
		decode_bitfield(f, upi2);
		break;

	case 6: /* Punit */
		Wprintf("Punit: ");
		f = EXTRACT(status, 16, 23);
		decode_bitfield(f, punit1);
		f = EXTRACT(status, 24, 31);
		decode_bitfield(f, punit2);
		break;

	case 12: /* B2CMI */
		Wprintf("B2CMI: ");
		f = EXTRACT(status, 16, 31);
		decode_bitfield(f, b2cmi1);
		break;

	case 13 ... 24: /* MCCHAN */
		Wprintf("MCCHAN: ");
		f = EXTRACT(status, 16, 23);
		switch (EXTRACT(status, 24, 31)) {
		case 0: decode_bitfield(f, mcchan0); break;
		case 1: decode_bitfield(f, mcchan1); break;
		case 2: decode_bitfield(f, mcchan2); break;
		case 4: decode_bitfield(f, mcchan4); break;
		case 8: decode_bitfield(f, mcchan8); break;
		case 32: decode_bitfield(f, mcchan32); break;
		case 33: decode_bitfield(f, mcchan33); break;
		}

		/* Decode MISC register if MISCV and OTHER_INFO[1] are both set */
		if (EXTRACT(status, 59, 59) && EXTRACT(status, 33, 33))
			granite_imc_misc(status, misc);
		break;
	}
}

void granite_memerr_misc(struct mce *m, int *channel, int *dimm)
{
	u64 status = m->status;
	unsigned int chan;

	/* Check this is a memory error */
	if (!test_prefix(7, status & 0xefff))
		return;

	chan = EXTRACT(status, 0, 3);
	if (chan == 0xf)
		return;

	if (m->bank < 13 || m->bank > 24)
		return;

	channel[0] = m->bank - 13;
}
