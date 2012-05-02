#ifndef _BATTERY_BATTERY_H
#define _BATTERY_BATTERY_H

/* User-visible state of the battery charger.
 *
 * If CHG_CONNECTED, power is externally supplied to the mobile.
 *
 * If CHG_ENABLED, the charger will try to provide charge
 * to the battery if needed, but this state might be switchable?
 *
 * BATTERY_CHARGING: Battery is not full, so a significant charging
 * current (not trickle charge) is supplied.
 *
 * BATTERY_FAILURE: Overtemperature, overvoltage, ... if this bit
 * is set, charging should be inhibited.
 */


enum battery_flags {
	BATTERY_CHG_CONNECTED = 1 << 0,  /* AC adapter is connected */
	BATTERY_CHG_ENABLED   = 1 << 1,  /* if needed charger could charge */
	BATTERY_CHARGING      = 1 << 2,  /* charger is actively charging */
	BATTERY_FAILURE       = 1 << 3,  /* problem exists preventing charge */
};

struct battery_info {
	enum battery_flags flags;
	int charger_volt_mV; /* charger connection voltage */
	int bat_volt_mV;     /* battery terminal voltage */
	int bat_chg_curr_mA; /* battery charging current */
	int battery_percent; /* 0(empty) .. 100(full) */
};

extern struct battery_info
battery_info;

#endif
