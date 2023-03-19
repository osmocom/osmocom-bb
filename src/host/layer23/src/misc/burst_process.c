#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/conv.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/isdn/v110.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/gsm0503.h>
#include <osmocom/gsm/gsm44021.h>
#include <l1ctl_proto.h>

#include "rlp.h"

struct gsmtap_inst *g_gti;

/* print a map of the de-interleaver to stdout */
static void deinterlieve_map(void)
{
       int j, k, B;
       int max_ib = 0;

       for (k = 0; k < 456; k++) {
	       /* upper bound for B: 4*n + 18 + 4 = 4*n + 22 */
	       B = /* B0 + 4n + */ (k % 19) + (k / 114);
	       /* upper bound for j: 18 + 19*5 = 113 */
	       j = (k % 19) + 19*(k % 6);
	       /* upper iB index: 4*n+23*114-1 */
	       //cB[k] = iB[B * 114 + j];
	       int ib = B * 114 + j;
	       printf("cB[%u] = iB[%u]\n", k, ib);
	       if (ib > max_ib)
		       max_ib = ib;
       }
       printf("max_ib=%u\n", max_ib);
}


/***********************************************************************
 * L2RCOP (TS 27.002)
 ***********************************************************************/

static int decode_l2rcop(FILE *f, const uint8_t *data, size_t data_len)
{
	size_t i = 0;

	while (i < data_len) {
		uint8_t status = data[i];
		uint8_t flags = status >> 5;
		uint8_t addr = status & 0x1f;

		if (flags)
			fprintf(f, "[%c%c%c] ", flags & 4 ? 'A' : 'a',
				flags & 2 ? 'B' : 'b', flags & 1 ? 'X' : 'x');

		switch (addr) {
		case 31:
			/* Last status change, remainder of L2RCOP-PDU empty. */
			goto out;
		case 30:
			/* Last status change, remainder of L2RCOP-PDU full of characters. */
			for (int j = i+1; j < data_len; j++)
				fputc(data[j], f);
			goto out;
		case 29:
			/* Destructive break signal, remainder of L2RCOP-PDU empty. */
			fprintf(f, "[BREAK] ");
			goto out;
		case 28:
			/* Destructive break acknowledge, remainder of L2RCOP-PDU empty. */
			fprintf(f, "[BREAK-ACK] ");
			goto out;
		case 27:
		case 26:
		case 25:
		case 24:
		case 0:
			/* reserved */
			return -EINVAL;
		default:
			/* 1 .. 23 */
			for (int j = i+1; j <= i+addr; j++)
				fputc(data[j], f);
			i += 1 + addr;
			break;
		}
	}

out:
	//fputc('\n', f);

	return 0;
}



/***********************************************************************
 * frame handling (call RLP decoder, print message)
 ***********************************************************************/

static void handle_rlp_frame(uint32_t fn, uint16_t band_arfcn, uint8_t ts, int8_t signal_dbm, int8_t snr, const uint8_t *data, size_t data_len)
{
	struct rlp_frame_decoded _rlp, *rlp = &_rlp;
	int rc;

	if (g_gti)
		gsmtap_send_ex(g_gti, GSMTAP_TYPE_GSM_RLP, band_arfcn, ts, GSMTAP_CHANNEL_TCH_F, 0, fn, signal_dbm, snr, data, data_len);

	rc = rlp_decode(rlp, 0, data, data_len);
	if (rc < 0)
		return;

	uint32_t fcs_calc_int = rlp_fcs_compute(data, data_len-3);
	if (fcs_calc_int != rlp->fcs)
		return;

	printf("fcs_calc=%06x", fcs_calc_int);

	switch (rlp->ftype) {
	case RLP_FT_U:
		printf("\tRLP:  U %s C/R=%u P/F=%u FCS=%06x\n", get_value_string(rlp_ftype_u_vals, rlp->u_ftype),
			rlp->c_r, rlp->p_f, rlp->fcs);
		break;
	case RLP_FT_S:
		printf("\tRLP:  S %s C/R=%u P/F=%u N(R)=%u FCS=%06x\n", get_value_string(rlp_ftype_s_vals, rlp->s_ftype),
			rlp->c_r, rlp->p_f, rlp->n_r, rlp->fcs);
		break;
	case RLP_FT_IS:
		printf("\tRLP: IS %s C/R=%u P/F=%u N(S)=%u N(R)=%u FCS=%06x %s\n", get_value_string(rlp_ftype_s_vals, rlp->s_ftype),
			rlp->c_r, rlp->p_f, rlp->n_s, rlp->n_r, rlp->fcs, osmo_hexdump(rlp->info, rlp->info_len));
		decode_l2rcop(stderr, rlp->info, rlp->info_len);
		break;

	}
}

struct fa_state {
	ubit_t v110_d[48*4];
	uint8_t v110_frame_nr;
};

static const uint8_t fax_sync[8] = { 0x3E, 0x37, 0x50, 0x96, 0xC1, 0xC8, 0xAF, 0x69 };

/* called by TRAU/FA synchronizer for every received FA frame */
static void fa_frame_cb(const ubit_t *bits, unsigned int num_bits)
{
	uint8_t packed[8];

	OSMO_ASSERT(num_bits == 64);

	osmo_ubit2pbit(packed, bits, num_bits);

	if (!memcmp(packed, fax_sync, sizeof(packed))) {
		printf("\t\tFA: SYNC\n");
	} else if (packed[0] /* == packed[2] == packed[4] == packed[6] */ == 0x11) {
		printf("\t\tFA: BCS-REC: 0x%02x\n", packed[1]);
	} else if (packed[0] /* == packed[2] == packed[4] == packed[6] */ == 0x33) {
		printf("\t\tFA: MSG-REC: 0x%02x\n", packed[1]);
	} else if (packed[0] /* == packed[2] == packed[4] == packed[6] */ == 0x44) {
		printf("\t\tFA: MSG-TRA: 0x%02x\n", packed[1]);
	} else {
		printf("\t\tFA: %s\n", osmo_hexdump(packed, sizeof(packed)));
		//printf("\t\t%s\n", osmo_ubit_dump(bits, num_bits));
	}
}

/* called for every (decoded) V.110 frame received over radio link */
static void handle_v110_frame(uint32_t fn, uint16_t band_arfcn, uint8_t ts, int8_t signal_dbm, int8_t snr, const struct osmo_v110_decoded_frame *fr)
{
	static struct fa_state fst_ul, fst_dl;
	struct fa_state *fst;

	if (band_arfcn & ARFCN_UPLINK)
		fst = &fst_ul;
	else
		fst = &fst_dl;

	//printf("\t%s\n", osmo_ubit_dump(fr->d_bits, sizeof(fr->d_bits)));

/* FIXME: establish sync with sync frames */
	memcpy(fst->v110_d + (fst->v110_frame_nr * 48), fr->d_bits, 48);
	if (fst->v110_frame_nr == 4 - 1) {
		fa_frame_cb(fst->v110_d + 0, 64);
		fa_frame_cb(fst->v110_d + 64, 64);
		fa_frame_cb(fst->v110_d + 128, 64);
	}
	fst->v110_frame_nr = (fst->v110_frame_nr + 1) % 4;
}

struct burst_state {
	sbit_t iB[22*114];
	uint8_t burst22_nr;
	uint8_t burst4_nr;
	bool initialized;
};

static void process_one_unmapped_burst(uint32_t fn, uint16_t band_arfcn, uint8_t ts, int8_t signal_dbm, int8_t snr, sbit_t *sbits)
{
	static struct burst_state bst_ul, bst_dl;
	struct burst_state *bst;
	uint8_t fn26 = fn % 26;

	if (band_arfcn & ARFCN_UPLINK)
		bst = &bst_ul;
	else
		bst = &bst_dl;

#if 1
	if (!bst->initialized) {
		if (fn26 != 2)
			return;
		bst->initialized = true;
	}
#endif

#if 0
	if (fn26 == 0)
		bst->burst22_nr = 0;
#endif

	/* copy in the new burst */
	memcpy(&bst->iB[(18 + bst->burst4_nr) * 114], sbits, 114);

	bst->burst4_nr++;
	if (bst->burst4_nr == 4) {
		sbit_t cB[456];
		ubit_t decoded[244];
		pbit_t dec_bytes[30];
		gsm0503_tch_f96_deinterleave(cB, bst->iB);
		printf("%10u: generated 456 deinterleaved bits\n", fn26);
		bst->burst4_nr = 0;
		osmo_conv_decode(&gsm0503_tch_f96, cB, decoded);
		//printf("\tdec_bin: %s\n", osmo_ubit_dump(decoded, 240));
#if 0
		/* non-transparent CSD (RLP) processing */
		osmo_ubit2pbit_ext(dec_bytes, 0, decoded, 0, 240, 1);
		//printf("\tdec_hex: %s\n", osmo_hexdump(dec_bytes, sizeof(dec_bytes)));
		handle_rlp_frame(fn, band_arfcn, ts, signal_dbm, snr, dec_bytes, sizeof(dec_bytes));
#else
		/* transparent CSD / FAX processing */
		for (int i = 0; i < 4; i++) {
			struct osmo_v110_decoded_frame fr;
			osmo_csd_12k_6k_decode_frame(&fr, decoded + i*60, 60);
			handle_v110_frame(fn, band_arfcn, ts, signal_dbm, snr, &fr);
		}
#endif

		/* move remainder of iB towards head */
		memmove(&bst->iB[0], &bst->iB[4*114], 18*114);
	}

#if 0
	bst->burst22_nr = (bst->burst22_nr) + 1 % 22;
#endif
}

static struct l1ctl_burst_ind *read_one_burst(int fd)
{
	static struct l1ctl_burst_ind bi;
	int rc;

	rc = read(fd, &bi, sizeof(bi));
	if (rc < fd) {
		fprintf(stderr, "Error reading from burst_fd (%d < %zu): %s\n", rc, sizeof(bi),
			strerror(errno));
		return NULL;
	}

	return &bi;
}

static int read_and_process_one_burst(int burst_fd)
{
	struct l1ctl_burst_ind *bi = read_one_burst(burst_fd);
	uint8_t ch_type, ch_subch, ch_ts;
	static bool started = false;
	char dir;

	if (!bi)
		return -1;


	/* skip initial noise before real data is received */
	if (!started) {
		if (bi->snr > 50)
			started = true;
	}
	if (!started)
		return 0;

	bi->frame_nr = ntohl(bi->frame_nr);
	bi->band_arfcn = ntohs(bi->band_arfcn);

	rsl_dec_chan_nr(bi->chan_nr, &ch_type, &ch_subch, &ch_ts);

	if (bi->band_arfcn & ARFCN_UPLINK)
		dir = 'U';
	else
		dir = 'D';
#if 1
	/* skip uplink for now */
	if (dir == 'D')
		return 0;
#endif

	/* skip SACCH/gap */
	if (bi->frame_nr % 26 == 12 || bi->frame_nr % 26 == 25)
		return 0;

	printf("%10u %2u %4d %c % 4d %3u %u/%u\n", bi->frame_nr, bi->frame_nr % 26,
		bi->band_arfcn&~ARFCN_FLAG_MASK,
		dir, rxlev2dbm(bi->rx_level), bi->snr, ch_ts, ch_subch);

	/* we know our recording was always on TCH/F of TS4 */
	if (ch_type != RSL_CHAN_Bm_ACCHs || ch_subch != 0 || ch_ts != 4)
		return 0;

	/* unpack the burst; it already is just the 114 (57+57) bits */
	ubit_t burst_ub[114];
	osmo_pbit2ubit(burst_ub, bi->bits, sizeof(burst_ub));

	/* convert from hard to soft bits */
	sbit_t burst_sb[114];
	for (int i=0; i<114; i++)
                burst_sb[i] = burst_ub[i] ? - (bi->snr >> 1) : (bi->snr >> 1);

	process_one_unmapped_burst(bi->frame_nr, bi->band_arfcn, ch_ts, rxlev2dbm(bi->rx_level), bi->snr, burst_sb);

	return 0;
}







int main(int argc, char **argv)
{
	const char *fname = argv[1];
	int rc, burst_fd;

	//deinterlieve_map();

	if (argc < 2) {
		fprintf(stderr, "Please specify burst_ind data file\n");
		exit(2);
	}

	burst_fd = open(fname, O_RDONLY);
	if (burst_fd < 0) {
		fprintf(stderr, "Error opening %s: %s\n", fname, strerror(errno));
		exit(1);
	}

	g_gti = gsmtap_source_init("localhost", GSMTAP_UDP_PORT, 0);
	gsmtap_source_add_sink(g_gti);

	while (1) {
		rc = read_and_process_one_burst(burst_fd);
		if (rc < 0)
			break;
	}
}
