#include <battery/battery.h>

/* Battery Management: Dummy file when no charging logic exists. */
struct battery_info battery_info;

void battery_dummy_init(){
	battery_info.flags = BATTERY_FAILURE; /* not implemented */
}

