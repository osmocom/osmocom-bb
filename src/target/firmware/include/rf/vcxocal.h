/*
 * Old OsmocomBB fw code used the same fixed and hard-coded numbers for
 * the slope of the VCXO and the initial AFC DAC setting for all Calypso
 * targets, but this approach is incorrect because different phone/modem
 * board designs use different VC(TC)XO components with different properties,
 * and some manufacturers have done per-unit calibration of their VC(TC)XO.
 *
 * We now have global variables in which these configuration or calibration
 * values are stored, and this header file provides the extern definitions
 * for these global vars.
 */

extern int16_t afc_initial_dac_value, afc_slope;
