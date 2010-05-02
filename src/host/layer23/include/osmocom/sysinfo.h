#ifndef _SYSINFO_H
#define _SYSINFO_H

/* collection of system information of the current cell */

/* frequency mask flags of frequency type */
#define	FREQ_TYPE_SERV		0x01 /* frequency of the serving cell */
#define	FREQ_TYPE_HOPP		0x02 /* frequency used for channel hopping */
#define	FREQ_TYPE_NCELL		0x1c /* frequency of the neighbor cell */
#define	FREQ_TYPE_NCELL_2	0x04 /* sub channel of SI 2 */
#define	FREQ_TYPE_NCELL_2bis	0x08 /* sub channel of SI 2bis */
#define	FREQ_TYPE_NCELL_2ter	0x10 /* sub channel of SI 2ter */
#define	FREQ_TYPE_REP		0xe0 /* frequency to be reported */
#define	FREQ_TYPE_REP_5		0x20 /* sub channel of SI 5 */
#define	FREQ_TYPE_REP_5bis	0x40 /* sub channel of SI 5bis */
#define	FREQ_TYPE_REP_5ter	0x80 /* sub channel of SI 5ter */

/* structure of one frequency */
struct gsm_sysinfo_freq {
	/* if the frequency included in the sysinfo */
	uint8_t	mask;
};

/* structure of all received system informations */
struct gsm48_sysinfo {
	/* flags of available information */
	uint8_t				si1, si2, si2bis, si2ter, si3,
					si4, si5, si5bis, si5ter, si6;

	/* memory maps to simply detect change in system info messages */
	uint8_t				si1_msg[23];
	uint8_t				si2_msg[23];
	uint8_t				si2b_msg[23];
	uint8_t				si2t_msg[23];
	uint8_t				si3_msg[23];
	uint8_t				si4_msg[23];
	uint8_t				si5_msg[18];
	uint8_t				si5b_msg[18];
	uint8_t				si5t_msg[18];
	uint8_t				si6_msg[18];

	struct	gsm_sysinfo_freq	freq[1024]; /* all frequencies */
	uint16_t			hopping[64]; /* hopping arfcn */
	uint8_t				hopp_len;

	/* serving cell */
	uint16_t			cell_id;
	uint16_t			mcc, mnc, lac; /* LAI */
	uint8_t				max_retrans; /* decoded */
	uint8_t				tx_integer; /* decoded */
	uint8_t				reest_denied; /* 1 = denied */
	uint8_t				cell_barr; /* 1 = barred */
	uint16_t			class_barr; /* bit 10 is emergency */
	/* cell selection */
	int8_t				ms_txpwr_max_ccch;
	int8_t				cell_resel_hyst_db;
	int8_t				rxlev_acc_min_db;
	uint8_t				neci;
	uint8_t				acs;
	/* bcch options */
	uint8_t				bcch_radio_link_timeout;
	uint8_t				bcch_dtx;
	uint8_t				bcch_pwrc;
	/* sacch options */
	uint8_t				sacch_radio_link_timeout;
	uint8_t				sacch_dtx;
	uint8_t				sacch_pwrc;
	/* control channel */
	uint8_t				ccch_conf;
	uint8_t				bs_ag_blks_res;
	uint8_t				att_allowed;
	uint8_t				pag_mf_periods;
	int32_t				t3212; /* real value in seconds */
	/* channel description */
	uint8_t				tsc;
	uint8_t				h; /* using hopping */
	uint16_t			arfcn;
	uint8_t				maio;
	uint8_t				hsn;
	uint8_t				chan_nr; /* type, slot, sub slot */

	/* neighbor cell */
	uint8_t				nb_ext_ind_si2;
	uint8_t				nb_ba_ind_si2;
	uint8_t				nb_ext_ind_si2bis;
	uint8_t				nb_ba_ind_si2bis;
	uint8_t				nb_multi_rep_si2ter; /* see GSM 05.08 8.4.3 */
	uint8_t				nb_ext_ind_si5;
	uint8_t				nb_ba_ind_si5;
	uint8_t				nb_ext_ind_si5bis;
	uint8_t				nb_ba_ind_si5bis;
	uint8_t				nb_multi_rep_si5ter;
	uint8_t				nb_ncc_permitted;
	uint8_t				nb_max_retrans; /* decoded */
	uint8_t				nb_tx_integer; /* decoded */
	uint8_t				nb_reest_denied; /* 1 = denied */
	uint8_t				nb_cell_barr; /* 1 = barred */
	uint16_t			nb_class_barr; /* bit 10 is emergency */
};

int gsm48_sysinfo_dump(struct osmocom_ms *ms, struct gsm48_sysinfo *s);

#endif /* _SYSINFO_H */
