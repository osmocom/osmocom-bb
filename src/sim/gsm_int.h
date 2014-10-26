#include <sys/types.h>
#include <osmocom/sim/sim.h>

int osim_int_cprof_add_gsm(struct osim_file_desc *mf);
int osim_int_cprof_add_telecom(struct osim_file_desc *mf);

int gsm_hpplmn_decode(struct osim_decoded_data *dd,
		     const struct osim_file_desc *desc,
		     int len, uint8_t *data);

int gsm_lp_decode(struct osim_decoded_data *dd,
		 const struct osim_file_desc *desc,
		 int len, uint8_t *data);

int gsm_imsi_decode(struct osim_decoded_data *dd,
		   const struct osim_file_desc *desc,
		   int len, uint8_t *data);
