#ifndef _GSM411_SMS_H
#define _GSM411_SMS_H

#define SMS_HDR_SIZE	128
#define SMS_TEXT_SIZE	256

struct gsm_sms {
	unsigned long validity_minutes;
	uint8_t reply_path_req;
	uint8_t status_rep_req;
	uint8_t ud_hdr_ind;
	uint8_t protocol_id;
	uint8_t data_coding_scheme;
	uint8_t msg_ref;
	char address[20+1];	/* DA LV is 12 bytes max, i.e. 10 bytes
				 * BCD == 20 bytes string */
	time_t time;
	uint8_t user_data_len;
	uint8_t user_data[SMS_TEXT_SIZE];

	char text[SMS_TEXT_SIZE];
};

int gsm411_sms_init(struct osmocom_ms *ms);
int gsm411_sms_exit(struct osmocom_ms *ms);
struct gsm_sms *sms_alloc(void);
void sms_free(struct gsm_sms *sms);
struct gsm_sms *sms_from_text(const char *receiver, int dcs, const char *text);
int gsm411_rcv_sms(struct osmocom_ms *ms, struct msgb *msg);
int sms_send(struct osmocom_ms *ms, const char *sms_sca, const char *number,
	const char *text);

#endif /* _GSM411_SMS_H */
