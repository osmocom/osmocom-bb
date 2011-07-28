#ifndef OSMOCOM_GSM_0502_H
#define OSMOCOM_GSM_0502_H

#include <stdint.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

/* Table 5 Clause 7 TS 05.02 */
static inline unsigned int
gsm0502_get_n_pag_blocks(struct gsm48_control_channel_descr *chan_desc)
{
	if (chan_desc->ccch_conf == RSL_BCCH_CCCH_CONF_1_C)
		return 3 - chan_desc->bs_ag_blks_res;
	else
		return 9 - chan_desc->bs_ag_blks_res;
}

/* Chapter 6.5.2 of TS 05.02 */
static inline unsigned int
gsm0502_get_ccch_group(uint64_t imsi, unsigned int bs_cc_chans,
			unsigned int n_pag_blocks)
{
	return (imsi % 1000) % (bs_cc_chans * n_pag_blocks) / n_pag_blocks;
}

/* Chapter 6.5.2 of TS 05.02 */
static inline unsigned int
gsm0502_get_paging_group(uint64_t imsi, unsigned int bs_cc_chans,
			 int n_pag_blocks)
{
	return (imsi % 1000) % (bs_cc_chans * n_pag_blocks) % n_pag_blocks;
}

unsigned int
gsm0502_calc_paging_group(struct gsm48_control_channel_descr *chan_desc, uint64_t imsi);

#endif
