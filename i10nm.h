void i10nm_decode_model(int cputype, int bank, u64 status, u64 misc);
int i10nm_ce_type(int bank, u64 status, u64 misc);
void i10nm_memerr_misc(struct mce *m, int *channel, int *dimm);
