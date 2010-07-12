#ifndef _OSMOCORE_MNCC_H
#define _OSMOCORE_MNCC_H

#define GSM_MAX_FACILITY       128
#define GSM_MAX_SSVERSION      128
#define GSM_MAX_USERUSER       128

/* Expanded fields from GSM TS 04.08, Table 10.5.102 */
struct gsm_mncc_bearer_cap {
	int		transfer;	/* Information Transfer Capability */
	int 		mode;		/* Transfer Mode */
	int		coding;		/* Coding Standard */
	int		radio;		/* Radio Channel Requirement */
	int		speech_ctm;	/* CTM text telephony indication */
	int		speech_ver[8];	/* Speech version indication */
};

struct gsm_mncc_number {
	int 		type;
	int 		plan;
	int		present;
	int		screen;
	char		number[33];
};

struct gsm_mncc_cause {
	int		location;
	int		coding;
	int		rec;
	int		rec_val;
	int		value;
	int		diag_len;
	char		diag[32];
};

struct gsm_mncc_useruser {
	int		proto;
	char		info[GSM_MAX_USERUSER + 1]; /* + termination char */
};

struct gsm_mncc_progress {
	int		coding;
	int		location;
	int 		descr;
};

struct gsm_mncc_facility {
	int		len;
	char		info[GSM_MAX_FACILITY];
};

struct gsm_mncc_ssversion {
	int		len;
	char		info[GSM_MAX_SSVERSION];
};

struct gsm_mncc_cccap {
	int		dtmf;
	int		pcp;
};

enum {
	GSM_MNCC_BCAP_SPEECH	= 0,
	GSM_MNCC_BCAP_UNR_DIG	= 1,
	GSM_MNCC_BCAP_AUDIO	= 2,
	GSM_MNCC_BCAP_FAX_G3	= 3,
	GSM_MNCC_BCAP_OTHER_ITC = 5,
	GSM_MNCC_BCAP_RESERVED	= 7,
};

#endif
