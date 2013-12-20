/* test routines for BSSGP flow control implementation in libosmogb
 * (C) 2012 by Harald Welte <laforge@gnumonks.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

#include <osmocom/core/application.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gprs/gprs_bssgp.h>

static unsigned long in_ctr = 1;
static struct timeval tv_start;

int get_centisec_diff(void)
{
	struct timeval tv;
	struct timeval now;
	gettimeofday(&now, NULL);

	timersub(&now, &tv_start, &tv);

	return tv.tv_sec * 100 + tv.tv_usec/10000;
}

/* round to deciseconds to make sure test output is always consistent */
int round_decisec(int csec_in)
{
	int tmp = csec_in / 10;

	return tmp * 10;
}

static int fc_out_cb(struct bssgp_flow_control *fc, struct msgb *msg,
		     uint32_t llc_pdu_len, void *priv)
{
	unsigned int csecs = get_centisec_diff();
	csecs = round_decisec(csecs);

	printf("%u: FC OUT Nr %lu\n", csecs, (unsigned long) msg);
}

static int fc_in(struct bssgp_flow_control *fc, unsigned int pdu_len)
{
	unsigned int csecs = get_centisec_diff();
	csecs = round_decisec(csecs);

	printf("%u: FC IN Nr %lu\n", csecs, in_ctr);
	bssgp_fc_in(fc, (struct msgb *) in_ctr, pdu_len, NULL);
	in_ctr++;
}


static void test_fc(uint32_t bucket_size_max, uint32_t bucket_leak_rate,
		    uint32_t max_queue_depth, uint32_t pdu_len,
		    uint32_t pdu_count)
{
	struct bssgp_flow_control *fc = talloc_zero(NULL, struct bssgp_flow_control);
	int i;

	bssgp_fc_init(fc, bucket_size_max, bucket_leak_rate, max_queue_depth,
		      fc_out_cb);

	gettimeofday(&tv_start, NULL);

	for (i = 0; i < pdu_count; i++) {
		fc_in(fc, pdu_len);
		osmo_timers_check();
		osmo_timers_prepare();
		osmo_timers_update();
	}

	while (1) {
		usleep(100000);
		osmo_timers_check();
		osmo_timers_prepare();
		osmo_timers_update();

		if (llist_empty(&fc->queue))
			break;
	}
}

static void help(void)
{
	printf(" -h --help                This help message\n");
	printf(" -s --bucket-size-max N   Maximum size of bucket in octets\n");
	printf(" -r --bucket-leak-rate N  Bucket leak rate in octets/sec\n");
	printf(" -d --max-queue-depth N   Maximum length of pending PDU queue (msgs)\n");
	printf(" -l --pdu-length N        Length of each PDU in octets\n");
}

int bssgp_prim_cb(struct osmo_prim_hdr *oph, void *ctx)
{
	return -1;
}

static struct log_info info = {};

int main(int argc, char **argv)
{
	uint32_t bucket_size_max = 100;	/* octets */
	uint32_t bucket_leak_rate = 100; /* octets / second */
	uint32_t max_queue_depth = 5; /* messages */
	uint32_t pdu_length = 10; /* octets */
	uint32_t pdu_count = 20; /* messages */
	int c;

	static const struct option long_options[] = {
		{ "bucket-size-max", 1, 0, 's' },
		{ "bucket-leak-rate", 1, 0, 'r' },
		{ "max-queue-depth", 1, 0, 'd' },
		{ "pdu-length", 1, 0, 'l' },
		{ "pdu-count", 1, 0, 'c' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	osmo_init_logging(&info);
	log_set_use_color(osmo_stderr_target, 0);
	log_set_print_filename(osmo_stderr_target, 0);

	while ((c = getopt_long(argc, argv, "s:r:d:l:c:",
				long_options, NULL)) != -1) {
		switch (c) {
		case 's':
			bucket_size_max = atoi(optarg);
			break;
		case 'r':
			bucket_leak_rate = atoi(optarg);
			break;
		case 'd':
			max_queue_depth = atoi(optarg);
			break;
		case 'l':
			pdu_length = atoi(optarg);
			break;
		case 'c':
			pdu_count = atoi(optarg);
			break;
		case 'h':
			help();
			exit(EXIT_SUCCESS);
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	/* bucket leak rate less than 100 not supported! */
	if (bucket_leak_rate < 100) {
		fprintf(stderr, "Bucket leak rate < 100 not supported!\n");
		exit(EXIT_FAILURE);
	}

	printf("===== BSSGP flow-control test START\n");
	printf("size-max=%u oct, leak-rate=%u oct/s, "
		"queue-len=%u msgs, pdu_len=%u oct, pdu_cnt=%u\n\n", bucket_size_max,
		bucket_leak_rate, max_queue_depth, pdu_length, pdu_count);
	test_fc(bucket_size_max, bucket_leak_rate, max_queue_depth,
		pdu_length, pdu_count);
	printf("===== BSSGP flow-control test END\n\n");

	exit(EXIT_SUCCESS);
}
