#include <stdint.h>
#include <calypso/irq.h>

void irq_disable(enum irq_nr nr) {}
void irq_register_handler(enum irq_nr nr, irq_handler *handler) {}
void irq_config(enum irq_nr nr, int fiq, int edge, int8_t prio) {}
void irq_enable(enum irq_nr nr) {}
