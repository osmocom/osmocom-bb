#include <stdint.h>

void tpu_enqueue(uint16_t instr) {}
void tpu_enable(int active) {}
void tpu_wait_idle(void) {}
void tpu_frame_irq_en(int mcu, int dsp) {}
void tpu_init(void) {}
void tpu_reset(int active) {}
void tpu_rewind(void) {}
