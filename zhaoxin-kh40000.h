/* Zhaoxin KH-40000 model specific decoding */
#ifndef ZHAOXIN_KH40000_H
#define ZHAOXIN_KH40000_H

char *kh40000_bank_name(unsigned int bank);
void kh40000_decode_model(struct mce *m);
void kh40000_memerr_misc(struct mce *m, int *channel);

#endif
