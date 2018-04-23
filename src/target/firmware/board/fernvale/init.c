
// from fernly/include/fernvale-pmic.h
#define PMIC_ADDR 0xa0700a00
#define PMIC_CTRL9 (PMIC_ADDR + 0x24)
#define PMIC_CTRL10 (PMIC_ADDR + 0x28)

#include <memory.h>     // writel writeb

#include <uart.h>       // uart_init, uart_baudrate

void board_init(int with_irq)
{
    /* From fernly/main.c aka firmware, do_init(void) function */

    // was serial_init();
    uart_init(UART_MODEM, with_irq);
    // TODO setting baudrate is not supported yet.
    // uart_baudrate(UART_MODEM, UART_115200);

    // list_registers() for now TODO

    /* Disable system watchdog */
    writel(0x2200, 0xa0030000);

    /* Enable USB Download mode (required for no-battery operation) */
    writew(0x8000, PMIC_CTRL10);

    /* Disable battery watchdog */
    writew(0x2, PMIC_CTRL9);

    serial_puts("\n\nend of fernvale board_init()\n");

    // skip the reset for now...
    // was scriptic...
    // set_plls
    // enable_psram
    // bl 5
    // lcd init
    // lcd tpd
    // set_kbd // initializing the keypad
}
