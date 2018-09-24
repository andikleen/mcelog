void hsw_decode_model(int cputype, int bank, u64 status, u64 misc);
void haswell_ep_memerr_misc(struct mce *m, int *channel, int *dimm);
void haswell_memerr_misc(struct mce *m, int *channel, int *dimm);
