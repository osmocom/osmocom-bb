#ifndef _UWIRE_H
#define _UWIRE_H

void uwire_init(void);
int uwire_xfer(int cs, int bitlen, const void *dout, void *din);

#endif /* _UWIRE_H */
