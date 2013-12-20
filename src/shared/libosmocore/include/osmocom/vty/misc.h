#ifndef OSMO_VTY_MISC_H
#define OSMO_VTY_MISC_H

#include <osmocom/vty/vty.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/core/utils.h>

#define VTY_DO_LOWER		1
char *vty_cmd_string_from_valstr(void *ctx, const struct value_string *vals,
				 const char *prefix, const char *sep,
				 const char *end, int do_lower);

void vty_out_rate_ctr_group(struct vty *vty, const char *prefix,
                            struct rate_ctr_group *ctrg);

int osmo_vty_write_config_file(const char *filename);
int osmo_vty_save_config_file(void);

#endif
