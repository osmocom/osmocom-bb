// placeholder
#include <stdint.h>
#include <osmocom/core/utils.h>

// TODO CRAIG measure for fernvale
/* GSM900 ARFCN 33, Measurements by Steve Markgraf / May 2010 */
const int16_t dbm2apc_gsm900[] = {
	[0]     = 151,
	[1]     = 152,
	[2]     = 153,
	[3]     = 155,
	[4]     = 156,
	[5]     = 158,
	[6]     = 160,
	[7]     = 162,
	[8]     = 164,
	[9]     = 167,
	[10]    = 170,
	[11]    = 173,
	[12]    = 177,
	[13]    = 182,
	[14]    = 187,
	[15]    = 192,
	[16]    = 199,
	[17]    = 206,
	[18]    = 214,
	[19]    = 223,
	[20]    = 233,
	[21]    = 244,
	[22]    = 260,
	[23]    = 271,
	[24]    = 288,
	[25]    = 307,
	[26]    = 327,
	[27]    = 350,
	[28]    = 376,
	[29]    = 407,
	[30]    = 456,
	[31]    = 575,
};

const int dbm2apc_gsm900_max = ARRAY_SIZE(dbm2apc_gsm900) - 1;


