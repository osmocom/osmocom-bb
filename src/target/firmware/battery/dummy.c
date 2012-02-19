#include <battery/battery.h>

/* Battery Management: Dummy file when no charging logic exists. */
struct osmocom_battery_info osmocom_battery_info;

void battery_dummy_init(){
	osmocom_battery_info.flags = BATTERY_FAILURE; /* not implemented */
}

