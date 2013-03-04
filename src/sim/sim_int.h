#ifndef _SIM_INT_H

#include <osmocom/sim/sim.h>

struct osim_decoded_element *
element_alloc(struct osim_decoded_data *dd, const char *name,
	      enum osim_element_type type, enum osim_element_repr repr);

struct osim_decoded_element *
element_alloc_sub(struct osim_decoded_element *ee, const char *name,
	      enum osim_element_type type, enum osim_element_repr repr);

extern const struct osim_card_sw ts102221_uicc_sw[0];


void add_filedesc(struct osim_file_desc *root, const struct osim_file_desc *in, int num);
struct osim_file_desc *alloc_df(void *ctx, uint16_t fid, const char *name);
struct osim_file_desc *
add_df_with_ef(struct osim_file_desc *parent,
		uint16_t fid, const char *name,
		const struct osim_file_desc *in, int num);

struct osim_file_desc *
add_adf_with_ef(struct osim_file_desc *parent,
		const uint8_t *adf_name, uint8_t adf_name_len,
		const char *name, const struct osim_file_desc *in,
		int num);

struct osim_reader_ops {
	const char *name;
	struct osim_reader_hdl *(*reader_open)(int idx, const char *name, void *ctx);
	struct osim_card_hdl *(*card_open)(struct osim_reader_hdl *rh);
	int (*transceive)(struct osim_reader_hdl *rh, struct msgb *msg);
};

const struct osim_reader_ops pcsc_reader_ops;

#endif
