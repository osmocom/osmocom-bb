#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/write_queue.h>

static const struct log_info_cat default_categories[] = {
};

static const struct log_info log_info = {
	.cat = default_categories,
	.num_cat = ARRAY_SIZE(default_categories),
};

static void test_wqueue_limit(void)
{
	struct msgb *msg;
	struct osmo_wqueue wqueue;
	int rc;

	osmo_wqueue_init(&wqueue, 0);
	OSMO_ASSERT(wqueue.max_length == 0);
	OSMO_ASSERT(wqueue.current_length == 0);
	OSMO_ASSERT(wqueue.read_cb == NULL);
	OSMO_ASSERT(wqueue.write_cb == NULL);
	OSMO_ASSERT(wqueue.except_cb == NULL);

	/* try to add and fail */
	msg = msgb_alloc(4096, "msg1");
	rc = osmo_wqueue_enqueue(&wqueue, msg);
	OSMO_ASSERT(rc < 0);

	/* add one and fail on the second */
	wqueue.max_length = 1;
	rc = osmo_wqueue_enqueue(&wqueue, msg);
	OSMO_ASSERT(rc == 0);
	OSMO_ASSERT(wqueue.current_length == 1);
	msg = msgb_alloc(4096, "msg2");
	rc = osmo_wqueue_enqueue(&wqueue, msg);
	OSMO_ASSERT(rc < 0);

	/* add one more */
	wqueue.max_length = 2;
	rc = osmo_wqueue_enqueue(&wqueue, msg);
	OSMO_ASSERT(rc == 0);
	OSMO_ASSERT(wqueue.current_length == 2);

	/* release everything */
	osmo_wqueue_clear(&wqueue);
	OSMO_ASSERT(wqueue.current_length == 0);
	OSMO_ASSERT(wqueue.max_length == 2);

	/* Add two, fail on the third, free it and the queue */
	msg = msgb_alloc(4096, "msg3");
	rc = osmo_wqueue_enqueue(&wqueue, msg);
	OSMO_ASSERT(rc == 0);
	OSMO_ASSERT(wqueue.current_length == 1);
	msg = msgb_alloc(4096, "msg4");
	rc = osmo_wqueue_enqueue(&wqueue, msg);
	OSMO_ASSERT(rc == 0);
	OSMO_ASSERT(wqueue.current_length == 2);
	msg = msgb_alloc(4096, "msg5");
	rc = osmo_wqueue_enqueue(&wqueue, msg);
	OSMO_ASSERT(rc < 0);
	OSMO_ASSERT(wqueue.current_length == 2);
	msgb_free(msg);
	osmo_wqueue_clear(&wqueue);
}

int main(int argc, char **argv)
{
	struct log_target *stderr_target;

	log_init(&log_info, NULL);
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);
	log_set_print_filename(stderr_target, 0);

	test_wqueue_limit();

	printf("Done\n");
	return 0;
}
