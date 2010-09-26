#ifndef _L1_PRIM_H
#define _L1_PRIM_H

#include <stdint.h>

#include <layer1/tdma_sched.h>

struct l1ctl_fbsb_req;

/* Utils */
const uint8_t *pu_get_idle_frame(void);

/* Primitives tests/requests */
void l1s_fb_test(uint8_t base_fn, uint8_t fb_mode);
void l1s_sb_test(uint8_t base_fn);
void l1s_pm_test(uint8_t base_fn, uint16_t arfcn);
void l1s_nb_test(uint8_t base_fn);

void l1s_fbsb_req(uint8_t base_fn, struct l1ctl_fbsb_req *req);
void l1a_freq_req(uint32_t fn_sched);
void l1a_rach_req(uint8_t fn51, uint8_t mf_off, uint8_t ra);

/* Primitives raw scheduling sets */
extern const struct tdma_sched_item nb_sched_set[];
extern const struct tdma_sched_item nb_sched_set_ul[];

#endif /* _L1_PRIM_H */
