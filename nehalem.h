void nehalem_decode_model(u64 status, u64 misc);
void decode_memory_controller(u32 status);
void nehalem_memerr_misc(struct mce *m, int *channel, int *dimm);
