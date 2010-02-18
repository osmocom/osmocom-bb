#ifndef _L1_AVG_H
#define _L1_AVG_H

struct running_avg {
	/* configuration */
	uint16_t	period;			/* over how many samples to average */
	uint16_t	min_valid;

	int32_t		acc_val;
	uint16_t	num_samples;		/* how often did we try to sample? */
	uint16_t	num_samples_valid;	/* how often did we receive valid samples? */

	void		(*outfn)(struct running_avg *, int32_t avg);
	void		*priv;
};

/* input a new sample into the averaging process */
void runavg_input(struct running_avg *ravg, int32_t val, int valid);

/* check if sufficient samples have been obtained, and call outfn() */
int runavg_check_output(struct running_avg *ravg);

#endif /* _AVG_H */
