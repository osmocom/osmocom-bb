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

#include <stdio.h>

static void stat_test(void)
{
	enum test_items {
		TEST_A_ITEM,
		TEST_B_ITEM,
	};

	static const struct stat_item_desc item_description[] = {
		{ "item.a", "The A value", "ma", 4, -1 },
		{ "item.b", "The B value", "kb", 7, -1 },
	};

	static const struct stat_item_group_desc statg_desc = {
		.group_name_prefix = "test.one",
		.group_description = "Test number 1",
		.num_items = ARRAY_SIZE(item_description),
		.item_desc = item_description,
	};

	struct stat_item_group *statg =
		stat_item_group_alloc(NULL, &statg_desc, 0);

	struct stat_item_group *sgrp2;
	const struct stat_item *sitem1, *sitem2;
	int rc;
	int32_t value;
	int32_t rd_a = 0;
	int32_t rd_b = 0;
	int i;

	OSMO_ASSERT(statg != NULL);

	sgrp2 = stat_item_get_group_by_name_idx("test.one", 0);
	OSMO_ASSERT(sgrp2 == statg);

	sgrp2 = stat_item_get_group_by_name_idx("test.one", 1);
	OSMO_ASSERT(sgrp2 == NULL);

	sgrp2 = stat_item_get_group_by_name_idx("test.two", 0);
	OSMO_ASSERT(sgrp2 == NULL);

	sitem1 = stat_item_get_by_name(statg, "item.c");
	OSMO_ASSERT(sitem1 == NULL);

	sitem1 = stat_item_get_by_name(statg, "item.a");
	OSMO_ASSERT(sitem1 != NULL);
	OSMO_ASSERT(sitem1 == statg->items[TEST_A_ITEM]);

	sitem2 = stat_item_get_by_name(statg, "item.b");
	OSMO_ASSERT(sitem2 != NULL);
	OSMO_ASSERT(sitem2 != sitem1);
	OSMO_ASSERT(sitem2 == statg->items[TEST_B_ITEM]);

	value = stat_item_get_last(statg->items[TEST_A_ITEM]);
	OSMO_ASSERT(value == -1);

	rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);

	stat_item_set(statg->items[TEST_A_ITEM], 1);

	value = stat_item_get_last(statg->items[TEST_A_ITEM]);
	OSMO_ASSERT(value == 1);

	rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 1);
	OSMO_ASSERT(value == 1);

	rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);

	for (i = 2; i <= 32; i++) {
		stat_item_set(statg->items[TEST_A_ITEM], i);
		stat_item_set(statg->items[TEST_B_ITEM], 1000 + i);

		rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
		OSMO_ASSERT(rc == 1);
		OSMO_ASSERT(value == i);

		rc = stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
		OSMO_ASSERT(rc == 1);
		OSMO_ASSERT(value == 1000 + i);
	}

	/* Keep 2 in FIFO */
	stat_item_set(statg->items[TEST_A_ITEM], 33);
	stat_item_set(statg->items[TEST_B_ITEM], 1000 + 33);

	for (i = 34; i <= 64; i++) {
		stat_item_set(statg->items[TEST_A_ITEM], i);
		stat_item_set(statg->items[TEST_B_ITEM], 1000 + i);

		rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
		OSMO_ASSERT(rc == 1);
		OSMO_ASSERT(value == i-1);

		rc = stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
		OSMO_ASSERT(rc == 1);
		OSMO_ASSERT(value == 1000 + i-1);
	}

	rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 1);
	OSMO_ASSERT(value == 64);

	rc = stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
	OSMO_ASSERT(rc == 1);
	OSMO_ASSERT(value == 1000 + 64);

	/* Overrun FIFOs */
	for (i = 65; i <= 96; i++) {
		stat_item_set(statg->items[TEST_A_ITEM], i);
		stat_item_set(statg->items[TEST_B_ITEM], 1000 + i);
	}

	rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 93 - 65 + 1);
	OSMO_ASSERT(value == 93);

	for (i = 94; i <= 96; i++) {
		rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
		OSMO_ASSERT(rc == 1);
		OSMO_ASSERT(value == i);
	}

	rc = stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
	OSMO_ASSERT(rc == 90 - 65 + 1);
	OSMO_ASSERT(value == 1000 + 90);

	for (i = 91; i <= 96; i++) {
		rc = stat_item_get_next(statg->items[TEST_B_ITEM], &rd_b, &value);
		OSMO_ASSERT(rc == 1);
		OSMO_ASSERT(value == 1000 + i);
	}

	/* Test Discard */
	stat_item_set(statg->items[TEST_A_ITEM], 97);
	rc = stat_item_discard(statg->items[TEST_A_ITEM], &rd_a);
	OSMO_ASSERT(rc == 1);

	rc = stat_item_discard(statg->items[TEST_A_ITEM], &rd_a);
	OSMO_ASSERT(rc == 0);

	rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);

	stat_item_set(statg->items[TEST_A_ITEM], 98);
	rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 1);
	OSMO_ASSERT(value == 98);

	rc = stat_item_get_next(statg->items[TEST_A_ITEM], &rd_a, &value);
	OSMO_ASSERT(rc == 0);

	stat_item_group_free(statg);

	sgrp2 = stat_item_get_group_by_name_idx("test.one", 0);
	OSMO_ASSERT(sgrp2 == NULL);
}

int main(int argc, char **argv)
{
	static const struct log_info log_info = {};
	log_init(&log_info, NULL);

	stat_item_init(NULL);

	stat_test();
	return 0;
}
