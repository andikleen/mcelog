char *k8_bank_name(unsigned num);
void decode_k8_mc(struct mce *mce, int *ismemerr);
int mce_filter_k8(struct mce *m);

#define K8_MCE_THRESHOLD_BASE        (MCE_EXTENDED_BANK + 1)      /* MCE_AMD */
#define K8_MCE_THRESHOLD_TOP         (K8_MCE_THRESHOLD_BASE + 6 * 9)

#define K8_MCELOG_THRESHOLD_DRAM_ECC (4 * 9 + 0)
#define K8_MCELOG_THRESHOLD_LINK     (4 * 9 + 1)
#define K8_MCELOG_THRESHOLD_L3_CACHE (4 * 9 + 2)
#define K8_MCELOG_THRESHOLD_FBDIMM   (4 * 9 + 3)
