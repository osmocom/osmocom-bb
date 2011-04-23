/*
 * conv.h
 *
 * Copyright (C) 2011  Sylvain Munaut <tnt@246tNt.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __OSMO_CONV_H__
#define __OSMO_CONV_H__

#include <stdint.h>

#include <osmocom/core/bits.h>

struct osmo_conv_code {
	int N;
	int K;
	int len;

	const uint8_t (*next_output)[2];
	const uint8_t (*next_state)[2];

	const uint8_t *next_term_output;
	const uint8_t *next_term_state;

	const int *puncture;
};


/* Encoding */

	/* Low level API */
struct osmo_conv_encoder {
	const struct osmo_conv_code *code;
	int i_idx;	/* Next input bit index */
	int p_idx;	/* Current puncture index */
	uint8_t state;	/* Current state */
};

void osmo_conv_encode_init(struct osmo_conv_encoder *encoder,
                           const struct osmo_conv_code *code);
int  osmo_conv_encode_raw(struct osmo_conv_encoder *encoder,
                          const ubit_t *input, ubit_t *output, int n);
int  osmo_conv_encode_finish(struct osmo_conv_encoder *encoder, ubit_t *output);

	/* All-in-one */
int  osmo_conv_encode(const struct osmo_conv_code *code,
                      const ubit_t *input, ubit_t *output);


/* Decoding */

	/* Low level API */
struct osmo_conv_decoder {
	const struct osmo_conv_code *code;

	int n_states;

	int len;		/* Max o_idx (excl. termination) */

	int o_idx;		/* output index */
	int p_idx;		/* puncture index */

	unsigned int *ae;	/* accumulater error */
	unsigned int *ae_next;	/* next accumulated error (tmp in scan) */
	uint8_t *state_history;	/* state history [len][n_states] */
};

void osmo_conv_decode_init(struct osmo_conv_decoder *decoder,
                           const struct osmo_conv_code *code, int len);
void osmo_conv_decode_reset(struct osmo_conv_decoder *decoder);
void osmo_conv_decode_deinit(struct osmo_conv_decoder *decoder);

int osmo_conv_decode_scan(struct osmo_conv_decoder *decoder,
                          const sbit_t *input, int n);
int osmo_conv_decode_finish(struct osmo_conv_decoder *decoder,
                            const sbit_t *input);
int osmo_conv_decode_get_output(struct osmo_conv_decoder *decoder,
                                ubit_t *output, int has_finish);

	/* All-in-one */
int osmo_conv_decode(const struct osmo_conv_code *code,
                     const sbit_t *input, ubit_t *output);


#endif /* __OSMO_CONV_H__ */
