/* Zhaoxin CPU machine check decoding for mcelog. */
#ifndef ZHAOXIN_H
#define ZHAOXIN_H

enum cputype select_zhaoxin_cputype(int family, int model);
int mce_filter_zhaoxin(struct mce *m, unsigned int recordlen);
char *zhaoxin_bank_name(unsigned int num);
void decode_zhaoxin_mc(struct mce *m, int cputype, int *ismemerr, unsigned int recordlen);

#endif
