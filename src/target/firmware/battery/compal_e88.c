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

/* battery is considered full/empty at these thresholds... */
#define VBAT_full   PHYSICAL_TO_ADC(4000,VBAT_LSB_uV)
#define VBAT_empty  PHYSICAL_TO_ADC(3200,VBAT_LSB_uV)

/* we declare overvoltage at this point... */
#define VBAT_fail   PHYSICAL_TO_ADC(4250,VBAT_LSB_uV)

/* DAC to ADC offsets in CC mode with my C123 
   IMEI 358317015976471, P329431014

   I/mA DAC     ADC
   ----------------
   100	117	108
   150	176	168
   200	234	227
   250	293	291
   300	351	349
   350	410	410
*/

#define CHGDAC_GAIN   967  /* times 0.001 */
#define CHGDAC_OFFS    13

/* convert ADC reading to DAC value, according to calibration values
   given above */
#define CHGDAC_ADJ(x)   (CHGDAC_GAIN*(x)/1000+CHGDAC_OFFS)

/* charging current in DAC LSBs, same ref. and # of bits, but keep
   the correction specified above in mind!  */

#define ICHG_set    CHGDAC_ADJ(PHYSICAL_TO_ADC(200,ICHG_LSB_uA))
#define VCHG_set    CHGDAC_ADJ(VBAT_full)

struct battery_info battery_info;	/* global battery info */
uint16_t bat_compal_e88_madc[MADC_NUM_CHANNELS];	/* MADC measurements */

static const int BATTERY_TIMER_DELAY=5000; /* 5000ms for control loop */
static const int ADC_TIMER_DELAY=100;      /*  100ms for ADC conversion */

/* thermistor sense current, turn it up to eleven! */
#define  TH_SENS               (THSENS0|THSENS1|THSENS2|THEN)
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
 *  Failure modes TODO:
 *          + high battery temperature
 */
enum bat_compal_e88_chg_state {
	CHARG_OFF,
	CHARG_CONST_CURR,
	CHARG_CONST_VOLT,
	CHARG_FAIL
};
static enum bat_compal_e88_chg_state bat_compal_e88_chg_state;

static const char *bat_compal_e88_chg_state_names[]={
	"Off",
	"Constant Current",
	"Constant Voltage",
	"Battery Failure"
};

static void
bat_compal_e88_goto_state(enum bat_compal_e88_chg_state newstate){

	if(bat_compal_e88_chg_state == newstate) /* already there? */
		return;

	printf("\033[34;1mCHARGER: %s --> %s.\033[0m\n",
			bat_compal_e88_chg_state_names[bat_compal_e88_chg_state],
			bat_compal_e88_chg_state_names[newstate]);

	/* update user visible flags, set registers */
	switch(newstate){
	case CHARG_CONST_CURR:
		battery_info.flags &= ~BATTERY_FAILURE;
		battery_info.flags |= (BATTERY_CHG_ENABLED|
						BATTERY_CHARGING);
		twl3025_reg_write(BCICTL2,0);
		twl3025_reg_write(CHGREG,0);
		twl3025_reg_write(BCICTL2,CHEN|LEDC|CHIV);
		twl3025_reg_write(CHGREG,ICHG_set);

		break;

	case CHARG_CONST_VOLT:
		battery_info.flags &= ~( BATTERY_CHARGING |
						BATTERY_FAILURE );
		battery_info.flags |= BATTERY_CHG_ENABLED;
		twl3025_reg_write(BCICTL2,0);
		twl3025_reg_write(CHGREG,0);
		twl3025_reg_write(BCICTL2,CHEN|LEDC);
		twl3025_reg_write(CHGREG,VCHG_set);
		break;

	case CHARG_FAIL:
	case CHARG_OFF:
	default:
		battery_info.flags &= ~( BATTERY_CHG_ENABLED |
			BATTERY_CHARGING | BATTERY_FAILURE );
		twl3025_reg_write(BCICTL2,0); /* turn off charger */
		twl3025_reg_write(CHGREG,0);
		break;
	}

	printf("BCICTL2 is 0x%03x, CHGREG=%d\n",
			twl3025_reg_read(BCICTL2),
			twl3025_reg_read(CHGREG));

	bat_compal_e88_chg_state = newstate;
}

static void
bat_compal_e88_chg_control(){
	/* with AC power disconnected, always go to off state */
	if(!(battery_info.flags & BATTERY_CHG_CONNECTED)){
		bat_compal_e88_goto_state(CHARG_OFF);
		return;
	}

	/* if failure condition is detected, always goto failure state */
	if(bat_compal_e88_madc[MADC_VBAT] > VBAT_fail){
		bat_compal_e88_goto_state(CHARG_FAIL);
		return;
	}

	/* now AC power is present and battery is not over failure
	   thresholds */
	switch(bat_compal_e88_chg_state){
	case CHARG_OFF:
		if(bat_compal_e88_madc[MADC_VBAT] >= VBAT_full)
			bat_compal_e88_goto_state(CHARG_CONST_VOLT);
		else
			bat_compal_e88_goto_state(CHARG_CONST_CURR);
		break;
	case CHARG_CONST_CURR:
		if(bat_compal_e88_madc[MADC_VBAT] >= VBAT_full)
			bat_compal_e88_goto_state(CHARG_CONST_VOLT);
		break;
	case CHARG_CONST_VOLT:
		break;
	default:
	case CHARG_FAIL:
		if(bat_compal_e88_madc[MADC_VBAT] < VBAT_full)
			bat_compal_e88_goto_state(CHARG_CONST_CURR);
		break;
	}
}

/*
 * Charging voltage connection - state machine, remembers
 * state in battery_info.flags.
 *
 *                     VCHG > VCHG_thr_on
 * +-----------------+ ------------------> +---------------+
 * | ! CHG_CONNECTED |                     | CHG_CONNECTED |
 * +-----------------+ <------------------ +---------------+
 *                     VCHG < VCHG_thr_off
 */
static void
bat_compal_e88_chk_ac_presence(){
	int vrpcsts = twl3025_reg_read(VRPCSTS);

	/* check for presence of charging voltage */
	if(!(battery_info.flags & BATTERY_CHG_CONNECTED)){
		if(vrpcsts & CHGPRES){
			puts("\033[34;1mCHARGER: external voltage connected!\033[0m\n");
			battery_info.flags |= BATTERY_CHG_CONNECTED;

			/* always keep ADC, voltage dividers and bias voltages on */
			twl3025_unit_enable(TWL3025_UNIT_MAD,1);
			twl3025_reg_write(BCICTL1,BATTERY_ALL_SENSE);
		}
	} else {
		if(!(vrpcsts & CHGPRES)){
			/* we'll only run ADC on demand */
			twl3025_unit_enable(TWL3025_UNIT_MAD,0);
			twl3025_reg_write(BCICTL1,0);

			battery_info.flags &= ~ BATTERY_CHG_CONNECTED;
			puts("\033[34;1mCHARGER: external voltage disconnected!\033[0m\n");
		}
	}
}

/* ---- update voltages visible to the user ---- */
static void
bat_compal_e88_upd_measurements(){
	int adc,i;

	battery_info.charger_volt_mV=
		ADC_TO_PHYSICAL(bat_compal_e88_madc[MADC_VCHG],VCHG_LSB_uV);
	battery_info.bat_volt_mV=
		ADC_TO_PHYSICAL(bat_compal_e88_madc[MADC_VBAT],VBAT_LSB_uV);
	battery_info.bat_chg_curr_mA=
		ADC_TO_PHYSICAL(bat_compal_e88_madc[MADC_ICHG],ICHG_LSB_uA);

	adc = bat_compal_e88_madc[MADC_VBAT];
	if(adc <= VBAT_empty){			/* battery 0..100% */
		battery_info.battery_percent = 0;
	} else if (adc >= VBAT_full){
		battery_info.battery_percent = 100;
	} else {
		battery_info.battery_percent =
			(50+100*(adc-VBAT_empty))/(VBAT_full-VBAT_empty);
	}

        /* DEBUG */
        printf("BAT-ADC: ");
        for(i=0;i<MADC_NUM_CHANNELS;i++)
                printf("%3d ",bat_compal_e88_madc[i]);
        printf("%c\n",32);
        printf("\tCharger at %u mV.\n",battery_info.charger_volt_mV);
        printf("\tBattery at %u mV.\n",battery_info.bat_volt_mV);
        printf("\tCharging at %u mA.\n",battery_info.bat_chg_curr_mA);
        printf("\tBattery capacity is %u%%.\n",battery_info.battery_percent);
	printf("\tBattery range is %d..%d mV.\n",
		ADC_TO_PHYSICAL(VBAT_empty,VBAT_LSB_uV),
		ADC_TO_PHYSICAL(VBAT_full,VBAT_LSB_uV));
        printf("\tBattery full at %d LSB .. full at %d LSB\n",VBAT_empty,VBAT_full);
        printf("\tCharging at %d LSB (%d mA).\n",ICHG_set,
                ADC_TO_PHYSICAL(ICHG_set,ICHG_LSB_uA));
        i = twl3025_reg_read(BCICTL2);
        printf("\tBCICTL2=0x%03x\n",i);      
	printf("\tbattery-info.flags=0x%08x\n",battery_info.flags);
	printf("\tbat_compal_e88_chg_state=%d\n",bat_compal_e88_chg_state);
}

/* bat_compal_e88_adc_read() :
 *
 * Schedule a ADC conversion or read values from ADC. If we are
 * running on battery, bias currents/voltage dividers are turned
 * on on demand.
 *
 * Return 0 if new ADC values have been acquired, 1 if ADC
 * has been scheduled for a new conversion or is not yet finished.
 * 
 */

enum bat_compal_e88_madc_stat {
	ADC_CONVERSION = 1 << 0
};
static uint32_t bat_compal_e88_madc_stat=0;

static int
bat_compal_e88_adc_read(){
	int i;

	if(bat_compal_e88_madc_stat & ADC_CONVERSION){
		i = twl3025_reg_read(MADCSTAT);
		if(i & ADCBUSY)
			return 1;
		for(i=0;i<MADC_NUM_CHANNELS;i++)
			bat_compal_e88_madc[i]=twl3025_reg_read(VBATREG+i);
		/* if charger is connected, we keep the ADC and BIAS on
		   continuously, if running on battery, we try to save power */
		if(!(battery_info.flags & BATTERY_CHG_CONNECTED)){
			twl3025_reg_write(BCICTL1,0x00); /* turn off bias */
			twl3025_unit_enable(TWL3025_UNIT_MAD,0); /* & ADC */
		}
		bat_compal_e88_madc_stat &= ~ ADC_CONVERSION;
		return 0;
	} else {
		/* if running on battery, turn on ADC & BIAS on demand */
		if(!(battery_info.flags & BATTERY_CHG_CONNECTED)){
			twl3025_unit_enable(TWL3025_UNIT_MAD,1);
			twl3025_reg_write(BCICTL1,BATTERY_ALL_SENSE);
		}

		twl3025_reg_write(MADCTRL,0xff); /* convert all channels */
		twl3025_reg_write(VBATREG,0);    /* trigger conversion */

		bat_compal_e88_madc_stat |= ADC_CONVERSION;
		return 1;
	}
}

static void
battery_compal_e88_timer_cb(void *p){
	struct osmo_timer_list *tmr = (struct osmo_timer_list*)p;
	int i;

	if(bat_compal_e88_adc_read()){ /* read back ADCs after a brief delay */
		osmo_timer_schedule(tmr,ADC_TIMER_DELAY);
		return;
	}

	bat_compal_e88_upd_measurements();	/* convert user-accessible information */
	bat_compal_e88_chk_ac_presence();	/* detect AC charger presence */
	bat_compal_e88_chg_control();	/* battery charger state machine */

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

