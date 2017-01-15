/* tests for statistics */
/*
 * (C) 2015 Sysmocom s.m.f.c. GmbH
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
 *
 */

#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/stat_item.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/core/stats.h>

#include <stdio.h>

enum test_ctr {
	TEST_A_CTR,
	TEST_B_CTR,
};

static const struct rate_ctr_desc ctr_description[] = {
	[TEST_A_CTR] = { "ctr.a", "The A counter value"},
	[TEST_B_CTR] = { "ctr.b", "The B counter value"},
};

static const struct rate_ctr_group_desc ctrg_desc = {
	.group_name_prefix = "ctr-test.one",
	.group_description = "Counter test number 1",
	.num_ctr = ARRAY_SIZE(ctr_description),
	.ctr_desc = ctr_description,
	.class_id = OSMO_STATS_CLASS_SUBSCRIBER,
};

enum test_items {
	TEST_A_ITEM,
	TEST_B_ITEM,
};

static const struct osmo_stat_item_desc item_description[] = {
	[TEST_A_ITEM] = { "item.a", "The A value", "ma", 4, -1 },
	[TEST_B_ITEM] = { "item.b", "The B value", "kb", 7, -1 },
};

static const struct osmo_stat_item_group_desc statg_desc = {
	.group_name_prefix = "test.one",
	.group_description = "Test number 1",
	.num_items = ARRAY_SIZE(item_description),
	.item_desc = item_description,
	.class_id = OSMO_STATS_CLASS_PEER,
};

static void stat_test(void)
{
	struct osmo_stat_item_group *statg =
		osmo_stat_item_group_alloc(NULL, &statg_desc, 0);

	struct osmo_stat_item_group *sgrp2;
	const struct osmo_stat_item *sitem1, *sitem2;
	int rc;
	int32_t value;
	int32_t rd_a = 0;
	int32_t rd_b = 0;
	int i;

	OSMO_ASSERT(statg != NULL);

	sgrp2 = osmo_stat_item_get_group_by_name_idx("test.one", 0);
	OSMO_ASSERT(sgrp2 == statg);

	sgrp2 = osmo_stat_item_get_group_by_name_idx("test.one", 1);
	OSMO_ASSERT(sgrp2 == NULL);

	sgrp2 = osmo_stat_item_get_group_by_name_idx("test.two", 0);
	OSMO_ASSERT(sgrp2 == NULL);

	sitem1 = osmo_stat_item_get_by_name(statg, "item.c");
	OSMO_ASSERT(sitem1 == NULL);

	sitem1 = osmo_stat_item_get_by_name(statg, "item.a");
	OSMO_ASSERT(sitem1 != NULL);
	OSMO_ASSERT(sitem1 == statg->items[TEST_A_ITEM]);

	sitem2 = osmo_stat_item_get_by_name(statg, "item.b");
	OSMO_ASSERT(sitem2 != NULL);
	OSMO_ASSERT(sitem2 != sitem1);
	OSMO_ASSERT(sitem2 == statg->items[TEST_B_ITEM]);

	value = osmo_stat_item_get_last(statg->items[TEST_A_ITEM]);
	OSMO_ASSERT(value == -1);

	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);

	osmo_stat_item_set(statg->items[TEST_A_ITEM], 1);

	value = osmo_stat_item_get_last(statg->items[TEST_A_ITEM]);
	OSMO_ASSERT(value == 1);

	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc > 0);
	OSMO_ASSERT(value == 1);

	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);

	for (i = 2; i <= 32; i++) {
		osmo_stat_item_set(statg->items[TEST_A_ITEM], i);
		osmo_stat_item_set(statg->items[TEST_B_ITEM], 1000 + i);

		rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
		OSMO_ASSERT(rc > 0);
		OSMO_ASSERT(value == i);

		rc = osmo_stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
		OSMO_ASSERT(rc > 0);
		OSMO_ASSERT(value == 1000 + i);
	}

	/* Keep 2 in FIFO */
	osmo_stat_item_set(statg->items[TEST_A_ITEM], 33);
	osmo_stat_item_set(statg->items[TEST_B_ITEM], 1000 + 33);

	for (i = 34; i <= 64; i++) {
		osmo_stat_item_set(statg->items[TEST_A_ITEM], i);
		osmo_stat_item_set(statg->items[TEST_B_ITEM], 1000 + i);

		rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
		OSMO_ASSERT(rc > 0);
		OSMO_ASSERT(value == i-1);

		rc = osmo_stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
		OSMO_ASSERT(rc > 0);
		OSMO_ASSERT(value == 1000 + i-1);
	}

	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc > 0);
	OSMO_ASSERT(value == 64);

	rc = osmo_stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
	OSMO_ASSERT(rc > 0);
	OSMO_ASSERT(value == 1000 + 64);

	/* Overrun FIFOs */
	for (i = 65; i <= 96; i++) {
		osmo_stat_item_set(statg->items[TEST_A_ITEM], i);
		osmo_stat_item_set(statg->items[TEST_B_ITEM], 1000 + i);
	}

	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc > 0);
	OSMO_ASSERT(value == 93);

	for (i = 94; i <= 96; i++) {
		rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
		OSMO_ASSERT(rc > 0);
		OSMO_ASSERT(value == i);
	}

	rc = osmo_stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
	OSMO_ASSERT(rc > 0);
	OSMO_ASSERT(value == 1000 + 90);

	for (i = 91; i <= 96; i++) {
		rc = osmo_stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
		OSMO_ASSERT(rc > 0);
		OSMO_ASSERT(value == 1000 + i);
	}

	/* Test Discard (single item) */
	osmo_stat_item_set(statg->items[TEST_A_ITEM], 97);
	rc = osmo_stat_item_discard(statg->items[TEST_A_ITEM], &rd_a);
	OSMO_ASSERT(rc > 0);

	rc = osmo_stat_item_discard(statg->items[TEST_A_ITEM], &rd_a);
	OSMO_ASSERT(rc == 0);

	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);

	osmo_stat_item_set(statg->items[TEST_A_ITEM], 98);
	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc > 0);
	OSMO_ASSERT(value == 98);

	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);

	/* Test Discard (all items) */
	osmo_stat_item_set(statg->items[TEST_A_ITEM], 99);
	osmo_stat_item_set(statg->items[TEST_A_ITEM], 100);
	osmo_stat_item_set(statg->items[TEST_A_ITEM], 101);
	osmo_stat_item_set(statg->items[TEST_B_ITEM], 99);
	osmo_stat_item_set(statg->items[TEST_B_ITEM], 100);

	rc = osmo_stat_item_discard_all(&rd_a);
	rc = osmo_stat_item_discard_all(&rd_b);

	rc = osmo_stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);
	rc = osmo_stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
	OSMO_ASSERT(rc == 0);

	osmo_stat_item_group_free(statg);

	sgrp2 = osmo_stat_item_get_group_by_name_idx("test.one", 0);
	OSMO_ASSERT(sgrp2 == NULL);
}

/*** stats reporter tests ***/

/* define a special stats reporter for testing */

static int send_count;

enum {
	OSMO_STATS_REPORTER_TEST = OSMO_STATS_REPORTER_LOG + 1,
};

static int stats_reporter_test_send_counter(struct osmo_stats_reporter *srep,
	const struct rate_ctr_group *ctrg,
	const struct rate_ctr_desc *desc,
	int64_t value, int64_t delta)
{
	const char *group_name = ctrg ? ctrg->desc->group_name_prefix : "";

	printf("  %s: counter p=%s g=%s i=%u n=%s v=%lld d=%lld\n",
		srep->name,
		srep->name_prefix ? srep->name_prefix : "",
		group_name, ctrg ? ctrg->idx : 0,
		desc->name, (long long)value, (long long)delta);

	send_count += 1;
	return 0;
}

static int stats_reporter_test_send_item(struct osmo_stats_reporter *srep,
	const struct osmo_stat_item_group *statg,
	const struct osmo_stat_item_desc *desc, int value)
{
	printf("  %s: item p=%s g=%s i=%u n=%s v=%d u=%s\n",
		srep->name,
		srep->name_prefix ? srep->name_prefix : "",
		statg->desc->group_name_prefix, statg->idx,
		desc->name, value, desc->unit ? desc->unit : "");

	send_count += 1;
	return 0;
}

static int stats_reporter_test_open(struct osmo_stats_reporter *srep)
{
	printf("  %s: open\n", srep->name);
	return 0;
}

static int stats_reporter_test_close(struct osmo_stats_reporter *srep)
{
	printf("  %s: close\n", srep->name);
	return 0;
}

static struct osmo_stats_reporter *stats_reporter_create_test(const char *name)
{
	struct osmo_stats_reporter *srep;
	srep = osmo_stats_reporter_alloc(OSMO_STATS_REPORTER_TEST, name);

	srep->have_net_config = 0;

	srep->open = stats_reporter_test_open;
	srep->close = stats_reporter_test_close;
	srep->send_counter = stats_reporter_test_send_counter;
	srep->send_item = stats_reporter_test_send_item;

	return srep;
}


static void test_reporting()
{
	struct osmo_stats_reporter *srep1, *srep2, *srep;
	struct osmo_stat_item_group *statg1, *statg2;
	struct rate_ctr_group *ctrg1, *ctrg2;
	void *stats_ctx = talloc_named_const(NULL, 1, "stats test context");

	int rc;

	printf("Start test: %s\n", __func__);

	/* Allocate counters and items */
	statg1 = osmo_stat_item_group_alloc(stats_ctx, &statg_desc, 1);
	OSMO_ASSERT(statg1 != NULL);
	statg2 = osmo_stat_item_group_alloc(stats_ctx, &statg_desc, 2);
	OSMO_ASSERT(statg2 != NULL);
	ctrg1 = rate_ctr_group_alloc(stats_ctx, &ctrg_desc, 1);
	OSMO_ASSERT(ctrg1 != NULL);
	ctrg2 = rate_ctr_group_alloc(stats_ctx, &ctrg_desc, 2);
	OSMO_ASSERT(ctrg2 != NULL);

	srep1 = stats_reporter_create_test("test1");
	OSMO_ASSERT(srep1 != NULL);

	srep2 = stats_reporter_create_test("test2");
	OSMO_ASSERT(srep2 != NULL);

	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_TEST, "test1");
	OSMO_ASSERT(srep == srep1);
	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_TEST, "test2");
	OSMO_ASSERT(srep == srep2);

	rc = osmo_stats_reporter_enable(srep1);
	OSMO_ASSERT(rc >= 0);
	OSMO_ASSERT(srep1->force_single_flush);
	rc = osmo_stats_reporter_set_max_class(srep1, OSMO_STATS_CLASS_SUBSCRIBER);
	OSMO_ASSERT(rc >= 0);

	rc = osmo_stats_reporter_enable(srep2);
	OSMO_ASSERT(rc >= 0);
	OSMO_ASSERT(srep2->force_single_flush);
	rc = osmo_stats_reporter_set_max_class(srep2, OSMO_STATS_CLASS_SUBSCRIBER);
	OSMO_ASSERT(rc >= 0);

	printf("report (initial):\n");
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 16);

	printf("report (srep1 global):\n");
	/* force single flush */
	osmo_stats_reporter_set_max_class(srep1, OSMO_STATS_CLASS_GLOBAL);
	srep1->force_single_flush = 1;
	srep2->force_single_flush = 1;
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 8);

	printf("report (srep1 peer):\n");
	/* force single flush */
	osmo_stats_reporter_set_max_class(srep1, OSMO_STATS_CLASS_PEER);
	srep1->force_single_flush = 1;
	srep2->force_single_flush = 1;
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 12);

	printf("report (srep1 subscriber):\n");
	/* force single flush */
	osmo_stats_reporter_set_max_class(srep1, OSMO_STATS_CLASS_SUBSCRIBER);
	srep1->force_single_flush = 1;
	srep2->force_single_flush = 1;
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 16);

	printf("report (srep2 disabled):\n");
	/* force single flush */
	srep1->force_single_flush = 1;
	srep2->force_single_flush = 1;
	rc = osmo_stats_reporter_disable(srep2);
	OSMO_ASSERT(rc >= 0);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 8);

	printf("report (srep2 enabled, no flush forced):\n");
	rc = osmo_stats_reporter_enable(srep2);
	OSMO_ASSERT(rc >= 0);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 8);

	printf("report (should be empty):\n");
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 0);

	printf("report (group 1, counter 1 update):\n");
	rate_ctr_inc(&ctrg1->ctr[TEST_A_CTR]);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 2);

	printf("report (group 1, item 1 update):\n");
	osmo_stat_item_set(statg1->items[TEST_A_ITEM], 10);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 2);

	printf("report (remove statg1, ctrg1):\n");
	/* force single flush */
	srep1->force_single_flush = 1;
	srep2->force_single_flush = 1;
	osmo_stat_item_group_free(statg1);
	rate_ctr_group_free(ctrg1);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 8);

	printf("report (remove srep1):\n");
	/* force single flush */
	srep1->force_single_flush = 1;
	srep2->force_single_flush = 1;
	osmo_stats_reporter_free(srep1);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 4);

	printf("report (remove statg2):\n");
	/* force single flush */
	srep2->force_single_flush = 1;
	osmo_stat_item_group_free(statg2);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 2);

	printf("report (remove srep2):\n");
	/* force single flush */
	srep2->force_single_flush = 1;
	osmo_stats_reporter_free(srep2);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 0);

	printf("report (remove ctrg2, should be empty):\n");
	rate_ctr_group_free(ctrg2);
	send_count = 0;
	osmo_stats_report();
	OSMO_ASSERT(send_count == 0);

	/* Leak check */
	OSMO_ASSERT(talloc_total_blocks(stats_ctx) == 1);
	talloc_free(stats_ctx);

	printf("End test: %s\n", __func__);
}

int main(int argc, char **argv)
{
	static const struct log_info log_info = {};
	log_init(&log_info, NULL);

	osmo_stat_item_init(NULL);

	stat_test();
	test_reporting();
	return 0;
}
