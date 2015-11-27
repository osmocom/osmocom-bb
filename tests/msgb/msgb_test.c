/*
 * (C) 2014 by On-Waves
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
 *
 */

#include <stdlib.h>
#include <osmocom/core/application.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>
#include <setjmp.h>

#include <errno.h>

#include <string.h>

#define CHECK_RC(rc)	\
	if (rc != 0) {	\
		printf("Operation failed rc=%d on %s:%d\n", rc, __FILE__, __LINE__); \
		abort(); \
	}

static jmp_buf jmp_env;
static int jmp_env_valid = 0;
static void osmo_panic_raise(const char *fmt, va_list args)
{
	/*
	 * The args can include pointer values which are not suitable for
	 * regression testing. So just write the (hopefully constant) format
	 * string to stdout and write the full message to stderr.
	 */
	printf("%s", fmt);
	vfprintf(stderr, fmt, args);
	if (!jmp_env_valid)
		abort();
	longjmp(jmp_env, 1);
}

/* Note that this does not nest */
#define OSMO_PANIC_TRY(pE) (osmo_panic_try(pE, setjmp(jmp_env)))

static int osmo_panic_try(volatile int *exception, int setjmp_result)
{
	jmp_env_valid = setjmp_result == 0;
	*exception = setjmp_result;

	if (setjmp_result)
		fprintf(stderr, "Exception caught: %d\n", setjmp_result);

	return *exception == 0;
}

static void test_msgb_api()
{
	struct msgb *msg = msgb_alloc_headroom(4096, 128, "data");
	unsigned char *cptr = NULL;
	int rc;

	printf("Testing the msgb API\n");

	printf("Buffer: %s\n", msgb_hexdump(msg));
	OSMO_ASSERT(msgb_test_invariant(msg));
	cptr = msg->l1h = msgb_put(msg, 4);
	printf("put(4) -> data%+d\n", cptr - msg->data);
	printf("Buffer: %s\n", msgb_hexdump(msg));
	OSMO_ASSERT(msgb_test_invariant(msg));
	cptr = msg->l2h = msgb_put(msg, 4);
	printf("put(4) -> data%+d\n", cptr - msg->data);
	printf("Buffer: %s\n", msgb_hexdump(msg));
	OSMO_ASSERT(msgb_test_invariant(msg));
	cptr = msg->l3h = msgb_put(msg, 4);
	printf("put(4) -> data%+d\n", cptr - msg->data);
	printf("Buffer: %s\n", msgb_hexdump(msg));
	OSMO_ASSERT(msgb_test_invariant(msg));
	cptr = msg->l4h = msgb_put(msg, 4);
	printf("put(4) -> data%+d\n", cptr - msg->data);
	printf("Buffer: %s\n", msgb_hexdump(msg));
	OSMO_ASSERT(msgb_test_invariant(msg));
	OSMO_ASSERT(msgb_length(msg) == 16);
	cptr = msgb_push(msg, 4);
	printf("push(4) -> data%+d\n", cptr - msg->data);
	printf("Buffer: %s\n", msgb_hexdump(msg));
	OSMO_ASSERT(msgb_test_invariant(msg));
	OSMO_ASSERT(msgb_length(msg) == 20);
	rc = msgb_trim(msg, 16);
	printf("trim(16) -> %d\n", rc);
	CHECK_RC(rc);
	OSMO_ASSERT(msgb_test_invariant(msg));
	printf("Buffer: %s\n", msgb_hexdump(msg));
	OSMO_ASSERT(msgb_length(msg) == 16);

	cptr = msgb_get(msg, 4);
	printf("get(4) -> data%+d\n", cptr - msg->data);
	printf("Buffer: %s\n", msgb_hexdump(msg));
	OSMO_ASSERT(msgb_test_invariant(msg));
	OSMO_ASSERT(msgb_length(msg) == 12);

	printf("Test msgb_hexdump\n");
	msg->l1h = msg->head;
	printf("Buffer: %s\n", msgb_hexdump(msg));
	msg->l3h = msg->data;
	printf("Buffer: %s\n", msgb_hexdump(msg));
	msg->l3h = msg->head - 1;
	printf("Buffer: %s\n", msgb_hexdump(msg));

	msgb_free(msg);
}

static struct log_info info = {};

int main(int argc, char **argv)
{
	osmo_init_logging(&info);

	test_msgb_api();

	printf("Success.\n");

	return 0;
}
