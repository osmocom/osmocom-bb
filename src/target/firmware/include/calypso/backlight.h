#ifndef _CAL_BACKLIGHT_H
#define _CAL_BACKLIGHT_H

/* Switch backlight to PWL mode (or back) */
void bl_mode_pwl(int on);

/* Set the backlight level */
void bl_level(uint8_t level);

#endif /* CAL_BACKLIGHT_H */
