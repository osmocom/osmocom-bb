#ifndef _SCIPHONE_G2_H
#define _SCIPHONE_G2_H
/* Bluelans Sciphone G2 support */

/* Use of the Baseband Parallel Interface by the G2 board */
#define HB_TX		MTK_BPI(0)
#define PCS_RX		MTK_BPI(1)
#define LB_TX		MTK_BPI(2)
#define PA_EN		MTK_BPI(4)
#define BAND_SW		MTK_BPI(5)
#define MODE_PA		MTK_BPI(7)
#define RF_VCO_EN	MTK_BPI(9)

#define GPIO_GPS_PWR_EN	MTK_GPIO(19)
#define GPIO_WIFI_EN	MTK_GPIO(20)
#define GPIO_OP1_EN	MTK_GPIO(22)
#define GPIO_BT_PWR_EN	MTK_GPIO(39)
#define GPIO_BT_RST	MTK_GPIO(62)
#define GPIO_USB_CHR_ID	MTK_GPIO(73)
#define GPIO_FM_SCL	MTK_GPIO(46)
#define GPIO_FM_SDA	MTK_GPIO(47)
#define GPIO_GS_SCL	MTK_GPIO(48)
#define GPIO_GS_SDA	MTK_GPIO(58)
#define GPIO_GS_EN	MTK_GPIO(26)

#define GPIO_GPS_EINT	MTK_GPIO(42)

#define EINT_HEADSET	MTK_EINT(0)
#define EINT_BT		MTK_EINT(1)
#define EINT_GPS2GSM	MTK_EINT(2)
#define EINT_WIFI	MTK_EINT(3)

#define CLKM_BT_32k	MTK_CLKM(2)
#define CLKM_WIFI_32k	MTK_CLKM(3)
#define CLKM_FM_32k	MTK_CLKM(4)


#endif /* _SCIPHONE_G2_H */
