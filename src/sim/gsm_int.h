#include <sys/types.h>
#include <osmocom/sim/sim.h>

const struct osim_file_desc *sim_ef_in_gsm;
const size_t sim_ef_in_gsm_num;

const struct osim_file_desc *sim_ef_in_graphics;
const size_t sim_ef_in_graphics_num;

const struct osim_file_desc *sim_ef_in_telecom;
const size_t sim_ef_in_telecom_num;

int gsm_hpplmn_decode(struct osim_decoded_data *dd,
		     const struct osim_file_desc *desc,
		     int len, uint8_t *data);

int gsm_lp_decode(struct osim_decoded_data *dd,
		 const struct osim_file_desc *desc,
		 int len, uint8_t *data);

int gsm_imsi_decode(struct osim_decoded_data *dd,
		   const struct osim_file_desc *desc,
		   int len, uint8_t *data);
