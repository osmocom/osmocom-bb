/* Battery management for compal_e88 */

/* (C) 2010 by Christian Vogel <vogelchr@vogel.cx>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*
 * ___C123 (compal e88) very simplified diagram of charging circuitry___
 *
 *                           ICTL
 *                            |
 *                            v
 *  Charger -> OVP -------> P-Mosfet --->[0.15 Ohm]---> Battery
 *             (NCP345,  |             |            |
 *              6.85V)   v             v            v
 *                      VCHG          VCCS        VBAT &
 *                                                VBATS
 *
 * Inputs to IOTA:
 *  VCHG:        senses voltage on the charger input
 *  VCSS/VBATS:  to difference amplifier, to measure charge current
 *  VBAT:        senses voltage on the battery terminal
 * Outputs from IOTA:
 *  ICTL:        Control signal for the switching mosfet
 *
 */

#include <battery/battery.h>
#include <battery/compal_e88.h>

#include <stdint.h>
#include <abb/twl3025.h>
#include <comm/timer.h>
#include <stdio.h>

/* ADC calibration, scale is LSB/uV or LSB/uA, physical unit is mV or mA */
#define ADC_TO_PHYSICAL(adc,scale) (((adc)*(scale)+500)/1000)
#define PHYSICAL_TO_ADC(phy,scale) (((phy)*1000+(scale)/2)/(scale))

/* conversion factors for internal IOTA battery charging/sensing circuitry */
#define VREF_LSB_uV 1709  /* VREF = 1.75V    --> 1.709 mV/LSB */
#define VBAT_LSB_uV 6836  /* VBAT = 7.0 V FS --> 6.836 mV/LSB */
#define VCHG_LSB_uV 8545  /* VCHG = 8.75V FS --> 8.545 mV/LSB */
#define ICHG_LSB_uA 854   /* ICHG = 875mA FS --> 0.854 mA/LSB */

/* charger is considered plugged in/removed when over/under... */
#define VCHG_thr_on  PHYSICAL_TO_ADC(4500,VCHG_LSB_uV)
#define VCHG_thr_off PHYSICAL_TO_ADC(3200,VCHG_LSB_uV)

/* battery is considered full/empty at these thresholds... */
#define VBAT_full   PHYSICAL_TO_ADC(4100,VBAT_LSB_uV)
#define VBAT_empty  PHYSICAL_TO_ADC(3200,VBAT_LSB_uV)

/* we declare overvoltage at this point... */
#define VBAT_fail   PHYSICAL_TO_ADC(4250,VBAT_LSB_uV)

/* charging current in ADC LSBs */
#define ICHG_set    PHYSICAL_TO_ADC(200,ICHG_LSB_uA)
#define VCHG_set    VBAT_full

/* global battery info */
struct osmocom_battery_info osmocom_battery_info;

/* internal bookkeeping */
static uint16_t compal_e88_madc[8];	/* remembering last ADC values */

enum battery_compal_e88_status {
	ADC_CONVERSION = 1 << 0
};
static uint32_t battery_compal_e88_status;

static const int BATTERY_TIMER_DELAY=5000; /* 5000ms for control loop */
static const int ADC_TIMER_DELAY=100;      /*  100ms for ADC conversion */

/* thermistor sense current, turn it up to eleven! */
#define  TH_SENS	       (THSENS0|THSENS1|THSENS2|THEN)
#define  BATTERY_ALL_SENSE     (TH_SENS|MESBAT|TYPEN)

/*
 * charger modes state machine
 *
 *    +------------------+-------------------+
 *    |                  |                   | lost AC power
 *    |                  ^                   ^
 *    V  on AC power     |    @VBAT_full     |
 * +-----+        +------------+ -----> +------------+
 * | OFF | -----> | CONST_CURR |        | CONST_VOLT |
 * +-----+        +------------+        +------------+
 *    ^          ^        |                   |
 *    |         /failure  v                   v failure
 * +---------+ /  gone    |                   | condition
 * | FAILURE | <----------+-------------------+
 * +---------+
 *
 *  Failure modes currently detected:
 *          + high battery voltage
 *          + high battery temperature
 */
enum charger_state {
	CHARG_OFF,
	CHARG_CONST_CURR,
	CHARG_CONST_VOLT,
	CHARG_FAIL
};
static enum charger_state charger_state;

static void
charger_goto_state(enum charger_state newstate){
	charger_state=newstate;
}

static void
battery_charger_control(){
	/* with AC power disconnected, always go to off state */
	if(!osmocom_battery_info.flags & BATTERY_CHG_CONNECTED){
		charger_goto_state(CHARG_OFF);
		return;
	}
#if 0
	/* if failure condition is detected, always goto failure state */
	if(){
		charger_goto_state(CHARG_FAIL);
	}

	switch(charger_state){
	case CHARG_OFF:
	case CHARG_CONST_CURR:
	case CHARG_CONST_VOLT:
	case CHARG_FAIL:
	default:
#endif		
}

/*
 * Charging voltage connection - state machine:
 *
 *                     VCHG > VCHG_thr_on
 * +-----------------+ ------------------> +---------------+
 * | ! CHG_CONNECTED |                     | CHG_CONNECTED |
 * +-----------------+ <------------------ +---------------+
 *                     VCHG < VCHG_thr_off
 *
 */
static void
check_charg_volt_presence(){
	/* check for presence of charging voltage */
	if(!(osmocom_battery_info.flags & BATTERY_CHG_CONNECTED)){
		if(compal_e88_madc[MADC_VCHG] > VCHG_thr_on){
			printf("CHARGER: external voltage connected!\n");
			osmocom_battery_info.flags |= BATTERY_CHG_CONNECTED;

			/* always keep ADC, voltage dividers and bias voltages on */
			twl3025_unit_enable(TWL3025_UNIT_MAD,1);
			twl3025_reg_write(BCICTL1,BATTERY_ALL_SENSE);
		}
	} else {
		if(compal_e88_madc[MADC_VCHG] < VCHG_thr_off){
			/* we'll only run ADC on demand */
			twl3025_unit_enable(TWL3025_UNIT_MAD,0);
			twl3025_reg_write(BCICTL1,0);

			osmocom_battery_info.flags &= ~ BATTERY_CHG_CONNECTED;
			printf("CHARGER: external voltage disconnected!\n");
		}
	}
}

/* ---- update voltages visible to the user ---- */
static void
battery_update_measurements(){
	int adc,i;

	osmocom_battery_info.charger_volt_mV=
		ADC_TO_PHYSICAL(compal_e88_madc[MADC_VCHG],VCHG_LSB_uV);
	osmocom_battery_info.bat_volt_mV=
		ADC_TO_PHYSICAL(compal_e88_madc[MADC_VBAT],VBAT_LSB_uV);
	osmocom_battery_info.bat_chg_curr_mA=
		ADC_TO_PHYSICAL(compal_e88_madc[MADC_ICHG],ICHG_LSB_uA);

	adc = compal_e88_madc[MADC_VBAT];
	if(adc <= VBAT_empty){
		osmocom_battery_info.battery_percent = 0;
	} else if (adc >= VBAT_full){
		osmocom_battery_info.battery_percent = 100;
	} else {
		osmocom_battery_info.battery_percent =
			(50+100*(adc-VBAT_empty))/(VBAT_full-VBAT_empty);
	}

        /* DEBUG */
        printf("BAT-ADC: ");
        for(i=0;i<MADC_NUM_CHANNELS;i++)
                printf("%3d ",compal_e88_madc[i]);
        printf("%c\n\n",32);
        printf("\tCharger at %u mV.\n",osmocom_battery_info.charger_volt_mV);
        printf("\tBattery at %u mV.\n",osmocom_battery_info.bat_volt_mV);
        printf("\tCharging at %u mA.\n",osmocom_battery_info.bat_chg_curr_mA);
        printf("\tBattery capacity is %u%%.\n",osmocom_battery_info.battery_percent);
	printf("\tBattery range is %d..%d mV.\n",
		ADC_TO_PHYSICAL(VBAT_empty,VBAT_LSB_uV),
		ADC_TO_PHYSICAL(VBAT_full,VBAT_LSB_uV));
        printf("\tBattery full at %d LSB .. full at %d LSB\n",VBAT_empty,VBAT_full);
        printf("\tCharging at %d LSB (%d mA).\n",ICHG_set,
                ADC_TO_PHYSICAL(ICHG_set,ICHG_LSB_uA));
        i = twl3025_reg_read(BCICTL2);
        printf("\tBattery charger thresholds in ADC LSBs: on %d and off %d\n",
                        VCHG_thr_on,VCHG_thr_off);
        printf("\tBCICTL2=0x%03x\n",i);      
	printf("\tosmocom-battery-info.flags=0x%08x\n",osmocom_battery_info.flags);
}

/* battery_adc_read() starts a conversion on all ADC channels
   if battery_compal_e88_status & ADC_CONVERSION is not set and
   tries to read back values (and reset ADC_CONVERSION) if it currently
   is set. If it returns zero, conversion data is available, if it
   returns non-zero a conversion has been triggered and data should
   be available "soon". */

static int
battery_adc_read(){
	int i;

	if(battery_compal_e88_status & ADC_CONVERSION){
		i = twl3025_reg_read(MADCSTAT);
		if(i & ADCBUSY)
			return 1;
		for(i=0;i<MADC_NUM_CHANNELS;i++)
			compal_e88_madc[i]=twl3025_reg_read(VBATREG+i);
		/* if charger is connected, we keep the ADC and BIAS on
		   continuously, if running on battery, we try to save power */
		if(!osmocom_battery_info.flags & BATTERY_CHG_CONNECTED){
			twl3025_reg_write(BCICTL1,0x00); /* turn off bias */
			twl3025_unit_enable(TWL3025_UNIT_MAD,0); /* & ADC */
		}
		battery_compal_e88_status &= ~ ADC_CONVERSION;
		return 0;
	} else {
		/* if running on battery, turn on ADC & BIAS on demand */
		if(!osmocom_battery_info.flags & BATTERY_CHG_CONNECTED){
			twl3025_unit_enable(TWL3025_UNIT_MAD,1);
			twl3025_reg_write(BCICTL1,BATTERY_ALL_SENSE);
		}
		twl3025_reg_write(MADCTRL,0xff); /* convert all channels */
		twl3025_reg_write(VBATREG,0);    /* trigger conversion */

		battery_compal_e88_status |= ADC_CONVERSION;
		return 1;
	}
}

static void
battery_compal_e88_timer_cb(void *p){
	struct osmo_timer_list *tmr = (struct osmo_timer_list*)p;
	int i;

	if(battery_adc_read()){ /* read back ADCs after a brief delay */
		osmo_timer_schedule(tmr,ADC_TIMER_DELAY);
		return;
	}

	battery_update_measurements();

	check_charg_volt_presence();
	battery_charger_control();

	osmo_timer_schedule(tmr,BATTERY_TIMER_DELAY);
}

/* timer that fires the charging loop regularly */
static struct osmo_timer_list battery_compal88_timer = {
	.cb = &battery_compal_e88_timer_cb,
	.data = &battery_compal88_timer
};

void
battery_compal_e88_init(){
	printf("%s: starting up\n",__FUNCTION__);
	osmo_timer_schedule(&battery_compal88_timer,BATTERY_TIMER_DELAY);
}

