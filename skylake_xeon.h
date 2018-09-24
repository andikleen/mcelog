void skylake_s_decode_model(int cputype, int bank, u64 status, u64 misc);
int skylake_s_ce_type(int bank, u64 status, u64 misc);
void skylake_memerr_misc(struct mce *m, int *channel, int *dimm);
