#ifndef OSMO_VTY_MISC_H
#define OSMO_VTY_MISC_H

#include <osmocom/vty/vty.h>
#include <osmocom/core/rate_ctr.h>

void vty_out_rate_ctr_group(struct vty *vty, const char *prefix,
                            struct rate_ctr_group *ctrg);

int osmo_vty_write_config_file(const char *filename);
int osmo_vty_save_config_file(void);

#endif
