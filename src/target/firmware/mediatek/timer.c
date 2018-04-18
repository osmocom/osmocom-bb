#include <stdint.h>

uint16_t hwtimer_read(int num) { return 0; }
void hwtimer_enable(int num, int on) {}
void hwtimer_load(int num, uint16_t val) {}
void hwtimer_config(int num, uint8_t pre_scale, int auto_reload) {}
