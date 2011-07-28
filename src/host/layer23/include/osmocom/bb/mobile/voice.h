#ifndef _voice_h
#define _voice_h

int gsm_voice_init(struct osmocom_ms *ms);
int gsm_send_voice(struct osmocom_ms *ms, struct gsm_data_frame *data);

#endif /* _voice_h */
