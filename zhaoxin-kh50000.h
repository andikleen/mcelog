/* Zhaoxin KH-50000 model specific decoding */
#ifndef ZHAOXIN_KH50000_H
#define ZHAOXIN_KH50000_H

char *kh50000_bank_name(unsigned int bank);
void kh50000_decode_model(struct mce *m);
void kh50000_memerr_misc(struct mce *m, int *channel);

#endif
