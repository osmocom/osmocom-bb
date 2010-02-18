#ifndef _I2C_H
#define _I2C_H

int i2c_write(uint8_t chip, uint32_t addr, int alen, const uint8_t *buffer, int len);
void i2c_init(int speed, int slaveadd);

#endif /* I2C_H */
