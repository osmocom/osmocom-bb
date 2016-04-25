#include <osmocom/gsm/tlv.h>

static void check_tlv_parse(uint8_t **data, size_t *data_len,
			    uint8_t exp_tag, size_t exp_len, const uint8_t *exp_val)
{
	uint8_t *value;
	size_t value_len;
	uint8_t tag;
	int rc;
	uint8_t *saved_data = *data;
	size_t saved_data_len = *data_len;

	rc = osmo_match_shift_tlv(data, data_len, exp_tag ^ 1, NULL, NULL);
	OSMO_ASSERT(rc == 0);

	rc = osmo_match_shift_tlv(data, data_len, exp_tag, &value, &value_len);
	OSMO_ASSERT(rc == (int)value_len + 2);
	OSMO_ASSERT(value_len == exp_len);
	OSMO_ASSERT(memcmp(value, exp_val, exp_len) == 0);

	/* restore data/data_len */
	*data = saved_data;
	*data_len = saved_data_len;

	rc = osmo_shift_tlv(data, data_len, &tag, &value, &value_len);
	OSMO_ASSERT(rc == (int)value_len + 2);
	OSMO_ASSERT(tag == exp_tag);
	OSMO_ASSERT(value_len == exp_len);
	OSMO_ASSERT(memcmp(value, exp_val, exp_len) == 0);
}

static void check_tv_fixed_match(uint8_t **data, size_t *data_len,
				 uint8_t tag, size_t len, const uint8_t *exp_val)
{
	uint8_t *value;
	int rc;

	rc = osmo_match_shift_tv_fixed(data, data_len, tag ^ 1, len, NULL);
	OSMO_ASSERT(rc == 0);

	rc = osmo_match_shift_tv_fixed(data, data_len, tag, len, &value);
	OSMO_ASSERT(rc == (int)len + 1);
	OSMO_ASSERT(memcmp(value, exp_val, len) == 0);
}

static void check_v_fixed_shift(uint8_t **data, size_t *data_len,
				size_t len, const uint8_t *exp_val)
{
	uint8_t *value;
	int rc;

	rc = osmo_shift_v_fixed(data, data_len, len, &value);
	OSMO_ASSERT(rc == (int)len);
	OSMO_ASSERT(memcmp(value, exp_val, len) == 0);
}

static void check_lv_shift(uint8_t **data, size_t *data_len,
			   size_t exp_len, const uint8_t *exp_val)
{
	uint8_t *value;
	size_t value_len;
	int rc;

	rc = osmo_shift_lv(data, data_len, &value, &value_len);
	OSMO_ASSERT(rc == (int)value_len + 1);
	OSMO_ASSERT(value_len == exp_len);
	OSMO_ASSERT(memcmp(value, exp_val, exp_len) == 0);
}

static void check_tlv_match_data_len(size_t data_len, uint8_t tag, size_t len,
				     const uint8_t *test_data)
{
	uint8_t buf[300] = {0};

	uint8_t *unchanged_ptr = buf - 1;
	size_t unchanged_len = 0xdead;
	size_t tmp_data_len = data_len;
	uint8_t *value = unchanged_ptr;
	size_t value_len = unchanged_len;
	uint8_t *data = buf;

	OSMO_ASSERT(data_len <= sizeof(buf));

	tlv_put(data, tag, len, test_data);
	if (data_len < len + 2) {
		OSMO_ASSERT(-1 == osmo_match_shift_tlv(&data, &tmp_data_len,
					    tag, &value, &value_len));
		OSMO_ASSERT(tmp_data_len == 0);
		OSMO_ASSERT(data == buf + data_len);
		OSMO_ASSERT(value == unchanged_ptr);
		OSMO_ASSERT(value_len == unchanged_len);
	} else {
		OSMO_ASSERT(0 <= osmo_match_shift_tlv(&data, &tmp_data_len,
					   tag, &value, &value_len));
		OSMO_ASSERT(value != unchanged_ptr);
		OSMO_ASSERT(value_len != unchanged_len);
	}
}

static void check_tv_fixed_match_data_len(size_t data_len,
					  uint8_t tag, size_t len,
					  const uint8_t *test_data)
{
	uint8_t buf[300] = {0};

	uint8_t *unchanged_ptr = buf - 1;
	size_t tmp_data_len = data_len;
	uint8_t *value = unchanged_ptr;
	uint8_t *data = buf;

	OSMO_ASSERT(data_len <= sizeof(buf));

	tv_fixed_put(data, tag, len, test_data);

	if (data_len < len + 1) {
		OSMO_ASSERT(-1 == osmo_match_shift_tv_fixed(&data, &tmp_data_len,
						 tag, len, &value));
		OSMO_ASSERT(tmp_data_len == 0);
		OSMO_ASSERT(data == buf + data_len);
		OSMO_ASSERT(value == unchanged_ptr);
	} else {
		OSMO_ASSERT(0 <= osmo_match_shift_tv_fixed(&data, &tmp_data_len,
						tag, len, &value));
		OSMO_ASSERT(value != unchanged_ptr);
	}
}

static void check_v_fixed_shift_data_len(size_t data_len,
					 size_t len, const uint8_t *test_data)
{
	uint8_t buf[300] = {0};

	uint8_t *unchanged_ptr = buf - 1;
	size_t tmp_data_len = data_len;
	uint8_t *value = unchanged_ptr;
	uint8_t *data = buf;

	OSMO_ASSERT(data_len <= sizeof(buf));

	memcpy(data, test_data, len);

	if (data_len < len) {
		OSMO_ASSERT(-1 == osmo_shift_v_fixed(&data, &tmp_data_len,
						len, &value));
		OSMO_ASSERT(tmp_data_len == 0);
		OSMO_ASSERT(data == buf + data_len);
		OSMO_ASSERT(value == unchanged_ptr);
	} else {
		OSMO_ASSERT(0 <= osmo_shift_v_fixed(&data, &tmp_data_len,
					       len, &value));
		OSMO_ASSERT(value != unchanged_ptr);
	}
}

static void check_lv_shift_data_len(size_t data_len,
				    size_t len, const uint8_t *test_data)
{
	uint8_t buf[300] = {0};

	uint8_t *unchanged_ptr = buf - 1;
	size_t unchanged_len = 0xdead;
	size_t tmp_data_len = data_len;
	uint8_t *value = unchanged_ptr;
	size_t value_len = unchanged_len;
	uint8_t *data = buf;

	lv_put(data, len, test_data);
	if (data_len < len + 1) {
		OSMO_ASSERT(-1 == osmo_shift_lv(&data, &tmp_data_len,
					   &value, &value_len));
		OSMO_ASSERT(tmp_data_len == 0);
		OSMO_ASSERT(data == buf + data_len);
		OSMO_ASSERT(value == unchanged_ptr);
		OSMO_ASSERT(value_len == unchanged_len);
	} else {
		OSMO_ASSERT(0 <= osmo_shift_lv(&data, &tmp_data_len,
					  &value, &value_len));
		OSMO_ASSERT(value != unchanged_ptr);
		OSMO_ASSERT(value_len != unchanged_len);
	}
}

static void test_tlv_shift_functions()
{
	uint8_t test_data[1024];
	uint8_t buf[1024];
	uint8_t *data_end;
	unsigned i, len;
	uint8_t *data;
	size_t data_len;
	const uint8_t tag = 0x1a;

	printf("Test shift functions\n");

	for (i = 0; i < ARRAY_SIZE(test_data); i++)
		test_data[i] = (uint8_t)i;

	for (len = 0; len < 256; len++) {
		const unsigned iterations = sizeof(buf) / (len + 2) / 4;

		memset(buf, 0xee, sizeof(buf));
		data_end = data = buf;

		for (i = 0; i < iterations; i++) {
			data_end = tlv_put(data_end, tag, len, test_data);
			data_end = tv_fixed_put(data_end, tag, len, test_data);
			/* v_fixed_put */
			memcpy(data_end, test_data, len);
			data_end += len;
			data_end = lv_put(data_end, len, test_data);
		}

		data_len = data_end - data;
		OSMO_ASSERT(data_len <= sizeof(buf));

		for (i = 0; i < iterations; i++) {
			check_tlv_parse(&data, &data_len, tag, len, test_data);
			check_tv_fixed_match(&data, &data_len, tag, len, test_data);
			check_v_fixed_shift(&data, &data_len, len, test_data);
			check_lv_shift(&data, &data_len, len, test_data);
		}

		OSMO_ASSERT(data == data_end);

		/* Test at end of data */

		OSMO_ASSERT(-1 == osmo_match_shift_tlv(&data, &data_len, tag, NULL, NULL));
		OSMO_ASSERT(-1 == osmo_match_shift_tv_fixed(&data, &data_len, tag, len, NULL));
		OSMO_ASSERT((len ? -1 : 0) == osmo_shift_v_fixed(&data, &data_len, len, NULL));
		OSMO_ASSERT(-1 == osmo_shift_lv(&data, &data_len, NULL, NULL));

		/* Test invalid data_len */
		for (data_len = 0; data_len <= len + 2 + 1; data_len += 1) {
			check_tlv_match_data_len(data_len, tag, len, test_data);
			check_tv_fixed_match_data_len(data_len, tag, len, test_data);
			check_v_fixed_shift_data_len(data_len, len, test_data);
			check_lv_shift_data_len(data_len, len, test_data);
		}
	}
}

int main(int argc, char **argv)
{
	//osmo_init_logging(&info);

	test_tlv_shift_functions();

	printf("Done.\n");
	return EXIT_SUCCESS;
}
