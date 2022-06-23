typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define __u64 u64
#define __u32 u32
#define __u16 u16
#define __u8  u8

/* kernel structure: */

/* Fields are zero when not available */
struct mce {
	__u64 status;
	__u64 misc;
	__u64 addr;
	__u64 mcgstatus;
	__u64 ip;
	__u64 tsc;	/* cpu time stamp counter */
	__u64 time;	/* wall time_t when error was detected */
	__u8  cpuvendor;	/* cpu vendor as encoded in system.h */
	__u8  pad1;
	__u16 pad2;
	__u32 cpuid;	/* CPUID 1 EAX */
	__u8  cs;		/* code segment */
	__u8  bank;	/* machine check bank */
	__u8  cpu;	/* cpu number; obsolete; use extcpu now */
	__u8  finished;   /* entry is valid */
	__u32 extcpu;	/* linux cpu number that detected the error */
	__u32 socketid;	/* CPU socket ID */
	__u32 apicid;	/* CPU initial apic ID */
	__u64 mcgcap;	/* MCGCAP MSR: machine check capabilities of CPU */
	__u64 synd;	/* MCA_SYND MSR: only valid on SMCA systems */
	__u64 ipid;	/* MCA_IPID MSR: only valid on SMCA systems */
	__u64 ppin;	/* Protected Processor Inventory Number */
	__u32 microcode;/* Microcode revision */
	__u64 kflags;	/* Internal kernel use */
};

#define X86_VENDOR_INTEL	0
#define X86_VENDOR_CYRIX	1
#define X86_VENDOR_AMD		2
#define X86_VENDOR_UMC		3
#define X86_VENDOR_CENTAUR	5
#define X86_VENDOR_TRANSMETA	7
#define X86_VENDOR_NSC		8
#define X86_VENDOR_NUM		9

#define MCE_OVERFLOW 0		/* bit 0 in flags means overflow */

#define MCE_GET_RECORD_LEN   _IOR('M', 1, int)
#define MCE_GET_LOG_LEN      _IOR('M', 2, int)
#define MCE_GETCLEAR_FLAGS   _IOR('M', 3, int)

/* Software defined banks */
#define MCE_EXTENDED_BANK	128

#define MCE_THERMAL_BANK	(MCE_EXTENDED_BANK + 0)
#define MCE_TIMEOUT_BANK        (MCE_EXTENDED_BANK + 90)

#define MCE_APEI_BANK		255

#define MCI_THRESHOLD_OVER  (1ULL<<48)  /* threshold error count overflow */

#define MCI_STATUS_VAL   (1ULL<<63)  /* valid error */
#define MCI_STATUS_OVER  (1ULL<<62)  /* previous errors lost */
#define MCI_STATUS_UC    (1ULL<<61)  /* uncorrected error */
#define MCI_STATUS_EN    (1ULL<<60)  /* error enabled */
#define MCI_STATUS_MISCV (1ULL<<59)  /* misc error reg. valid */
#define MCI_STATUS_ADDRV (1ULL<<58)  /* addr reg. valid */
#define MCI_STATUS_PCC   (1ULL<<57)  /* processor context corrupt */
#define MCI_STATUS_S	 (1ULL<<56)  /* signalled */
#define MCI_STATUS_AR	 (1ULL<<55)  /* action-required */
#define MCI_STATUS_FWST  (1ULL<<37)  /* Firmware updated status indicator */

#define MCG_STATUS_RIPV  (1ULL<<0)   /* restart ip valid */
#define MCG_STATUS_EIPV  (1ULL<<1)   /* eip points to correct instruction */
#define MCG_STATUS_MCIP  (1ULL<<2)   /* machine check in progress */
#define MCG_STATUS_LMCES (1ULL<<3)   /* local machine check signaled */

#define MCG_CMCI_P		(1ULL<<10)   /* CMCI supported */
#define MCG_TES_P		(1ULL<<11)   /* Yellow bit cache threshold supported */
#define MCG_SER_P		(1ULL<<24)   /* MCA recovery / new status */
#define MCG_ELOG_P		(1ULL<<26)   /* Extended error log supported */
#define MCG_LMCE_P		(1ULL<<27)   /* Local machine check supported */

#define NELE(x) (sizeof(x)/sizeof(*(x)))
#define err(x) perror(x),exit(1)
#define sizeof_field(t, f) (sizeof(((t *)0)->f))
#define endof_field(t, f) (sizeof(((t *)0)->f) + offsetof(t, f))

#define round_up(x,y) (((x) + (y) - 1) & ~((y)-1))
#define roundup(x,y) (((x) + (y) - 1) / (y) * (y))
#define round_down(x,y) ((x) & ~((y)-1))

#define BITS_PER_INT (sizeof(unsigned) * 8)
#define BITS_PER_LONG (sizeof(unsigned long) * 8)

#ifdef __GNUC__
#define PRINTFLIKE __attribute__((format(printf,1,2)))
#define noreturn   __attribute__((noreturn))
#else
#define PRINTFLIKE 
#define noreturn
#endif

int Wprintf(char *fmt, ...) PRINTFLIKE;
void Eprintf(char *fmt, ...) PRINTFLIKE;
void SYSERRprintf(char *fmt, ...) PRINTFLIKE;
void Lprintf(char *fmt, ...) PRINTFLIKE;
void Gprintf(char *fmt, ...) PRINTFLIKE;

extern int open_logfile(char *fn);

#include "cputype.h"

enum option_ranges {
	O_COMMON = 500,
	O_DISKDB = 1000,
};

enum syslog_opt { 
	SYSLOG_LOG = (1 << 0),		/* normal decoding output to syslog */
	SYSLOG_REMARK = (1 << 1), 	/* special warnings to syslog */
	SYSLOG_ERROR  = (1 << 2),	/* errors during operation to syslog */
	SYSLOG_ALL = SYSLOG_LOG|SYSLOG_REMARK|SYSLOG_ERROR,
	SYSLOG_FORCE = (1 << 3),
};

extern void usage(void);
extern void no_syslog(void);
extern void argsleft(int ac, char **av);
extern char *processor_flags;
extern int force_tsc;
extern enum syslog_opt syslog_opt;
extern int syslog_level;
extern enum cputype cputype;
extern int filter_memory_errors;
extern int imc_log;
extern int max_corr_err_counters;
extern void set_imc_log(int cputype);
