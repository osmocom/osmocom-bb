
// from fernly/include/fernvale-pmic.h
#define PMIC_ADDR 0xa0700a00
#define PMIC_CTRL9 (PMIC_ADDR + 0x24)
#define PMIC_CTRL10 (PMIC_ADDR + 0x28)

#include <memory.h>     // writel writeb

void board_init(int with_irq)
{
    /* From fernly/main.c aka firmware, do_init(void) function */

    // skip serial_init(), list_registers() for now TODO

    /* Disable system watchdog */
    writel(0xa0030000, 0x2200);

    /* Enable USB Download mode (required for no-battery operation) */
    writew(PMIC_CTRL10, 0x8000);

    /* Disable battery watchdog */
    writew(PMIC_CTRL9, 0x2);

    // skip the reset for now...
    // set_plls, enable_psram
}
