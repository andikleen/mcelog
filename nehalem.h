void nehalem_decode_model(u64 status, u64 misc);
void xeon75xx_decode_model(struct mce *m, unsigned msize);
void decode_memory_controller(u32 status, u8 bank);
void nehalem_memerr_misc(struct mce *m, int *channel, int *dimm);
