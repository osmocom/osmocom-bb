#include <board.h>  // for board_init
#include <memory.h> // for writeb

// from fernly/includes/fernvale-kbd.h
#define BIG_LED_ADDR (0xA0700000 + 0x0C80)
#define BIG_LED_ON   (0x3)
#define BIG_LED_OFF  (0x0)

int main(void)
{
    board_init(0);
    
    int i;

    while (1) {
        for ( i=0; i<1000000; i++) {
            writeb(BIG_LED_OFF, BIG_LED_ADDR);
        }
        for ( i=0; i<1000000; i++) {
            writeb(BIG_LED_ON, BIG_LED_ADDR);
        }
    }
}
