#ifndef _CALYPSO_IRQ_H
#define _CALYPSO_IRQ_H

enum irq_nr {
	IRQ_WATCHDOG		= 0,
	IRQ_TIMER1		= 1,
	IRQ_TIMER2		= 2,
	IRQ_TSP_RX		= 3,
	IRQ_TPU_FRAME		= 4,
	IRQ_TPU_PAGE		= 5,
	IRQ_SIMCARD		= 6,
	IRQ_UART_MODEM		= 7,
	IRQ_KEYPAD_GPIO		= 8,
	IRQ_RTC_TIMER		= 9,
	IRQ_RTC_ALARM_I2C	= 10,
	IRQ_ULPD_GAUGING	= 11,
	IRQ_EXTERNAL		= 12,
	IRQ_SPI			= 13,
	IRQ_DMA			= 14,
	IRQ_API			= 15,
	IRQ_SIM_DETECT		= 16,
	IRQ_EXTERNAL_FIQ	= 17,
	IRQ_UART_IRDA		= 18,
	IRQ_ULPD_GSM_TIMER	= 19,
	IRQ_GEA			= 20,
	_NR_IRQ
};

typedef void irq_handler(enum irq_nr nr);

/* initialize IRQ driver and enable interrupts */
void irq_init(void);

/* enable a certain interrupt */
void irq_enable(enum irq_nr nr);

/* disable a certain interrupt */
void irq_disable(enum irq_nr nr);

/* configure a certain interrupt */
void irq_config(enum irq_nr nr, int fiq, int edge, int8_t prio);

/* register an interrupt handler */
void irq_register_handler(enum irq_nr nr, irq_handler *handler);

/* Install the exception handlers to where the ROM loader jumps */
void calypso_exceptions_install(void);

#endif /* _CALYPSO_IRQ_H */
