#ifndef _SPI_H
#define _SPI_H

void spi_init(void);
int spi_xfer(uint8_t dev_idx, uint8_t bitlen, const void *dout, void *din);

#endif /* _SPI_H */
