/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "sample_rate_converter.h"
#include "sample_rate_converter_filter.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample_rate_converter_filter, CONFIG_SAMPLE_RATE_CONVERTER_LOG_LEVEL);

#ifdef CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SIMPLE
#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
static q15_t filter_48khz_24kz_16bit_simple[] = {0x3ffe, 0x3fff};

static q15_t filter_48khz_16kz_16bit_simple[] = {0x2aaa, 0x2aaa, 0x2aab};
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32

static q31_t filter_48khz_24kz_32bit_simple[] = {0x3fffffff, 0x40000000};

static q31_t filter_48khz_16kz_32bit_simple[] = {0x2aaaaaaa, 0x2aaaaaaa, 0x2aaaaaab};
#endif
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SIMPLE */

#ifdef CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SMALL
#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
static q15_t filter_48khz_16khz_16bit_small[] = {
	0xfff7, 0xfff9, 0x0021, 0x0052, 0x0022, 0xff66, 0xfed7, 0xffac, 0x01df, 0x0319, 0x0091,
	0xfb02, 0xf847, 0xff3a, 0x1076, 0x234f, 0x2b82, 0x234f, 0x1076, 0xff3a, 0xf847, 0xfb02,
	0x0091, 0x0319, 0x01df, 0xffac, 0xfed7, 0xff66, 0x0022, 0x0052, 0x0021, 0xfff9, 0xfff7};

static q15_t filter_48khz_24khz_16bit_small[] = {
	0x02fd, 0x02cc, 0xff77, 0xfd31, 0x000a, 0x033d, 0x0017, 0xfb27, 0xfe81, 0x0656,
	0x0335, 0xf5dc, 0xf71d, 0x152f, 0x36eb, 0x36eb, 0x152f, 0xf71d, 0xf5dc, 0x0335,
	0x0656, 0xfe81, 0xfb27, 0x0017, 0x033d, 0x000a, 0xfd31, 0xff77, 0x02cc, 0x02fd};

#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
static q31_t filter_48khz_16khz_32bit_small[] = {
	0xff34e0fc, 0xfd20fa09, 0x00b8d644, 0x01b78c3a, 0x01b60933, 0xff379d35, 0xfcec701a,
	0xfde17e67, 0x020dad5a, 0x051dfe81, 0x027ae300, 0xfb0ed811, 0xf6869fd3, 0xfd49086d,
	0x0fea99ac, 0x24820268, 0x2d7765d4, 0x24820268, 0x0fea99ac, 0xfd49086d, 0xf6869fd3,
	0xfb0ed811, 0x027ae300, 0x051dfe81, 0x020dad5a, 0xfde17e67, 0xfcec701a, 0xff379d35,
	0x01b60933, 0x01b78c3a, 0x00b8d644, 0xfd20fa09, 0xff34e0fc};

static q31_t filter_48khz_24khz_32bit_small[] = {
	0x01770e99, 0x0250f59b, 0xfd7c8f2e, 0xffd20846, 0x020db128, 0x007755a1, 0xfcf0262e,
	0x000e8dea, 0x04101b61, 0xff2c414c, 0xfa778b34, 0x024bd49e, 0x07eaa09c, 0xfa9030bb,
	0xf2904614, 0x10a37d9b, 0x3be3e289, 0x3be3e289, 0x10a37d9b, 0xf2904614, 0xfa9030bb,
	0x07eaa09c, 0x024bd49e, 0xfa778b34, 0xff2c414c, 0x04101b61, 0x000e8dea, 0xfcf0262e,
	0x007755a1, 0x020db128, 0xffd20846, 0xfd7c8f2e, 0x0250f59b, 0x01770e99};
#endif
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SMALL */

int sample_rate_converter_filter_get(enum sample_rate_converter_filter filter_type,
				     uint8_t conversion_ratio, void **filter_ptr,
				     size_t *filter_size)
{

	__ASSERT(filter_ptr != NULL, "Filter pointer cannot be NULL");
	__ASSERT(filter_size != NULL, "Filter size pointer cannot be NULL");

	if ((conversion_ratio != 2) && (conversion_ratio != 3)) {
		LOG_ERR("Invalid conversion ratio: %d", conversion_ratio);
		return -EINVAL;
	}

#if CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
	switch (filter_type) {
#if CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SIMPLE
	case SAMPLE_RATE_FILTER_SIMPLE:
		if (conversion_ratio == 2) {
			*filter_ptr = filter_48khz_24kz_16bit_simple;
			*filter_size = ARRAY_SIZE(filter_48khz_24kz_16bit_simple);
		} else if (conversion_ratio == 3) {
			*filter_ptr = filter_48khz_16kz_16bit_simple;
			*filter_size = ARRAY_SIZE(filter_48khz_16kz_16bit_simple);
		}
		break;
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SIMPLE */
#if CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SMALL
	case SAMPLE_RATE_FILTER_SMALL:
		if (conversion_ratio == 2) {
			*filter_ptr = filter_48khz_24khz_16bit_small;
			*filter_size = ARRAY_SIZE(filter_48khz_24khz_16bit_small);
		} else if (conversion_ratio == 3) {
			*filter_ptr = filter_48khz_16khz_16bit_small;
			*filter_size = ARRAY_SIZE(filter_48khz_16khz_16bit_small);
		}
		break;
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SMALL */
	default:
		LOG_ERR("No matching filter found\n");
		return -EINVAL;
	}
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16 */

#if CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
	switch (filter_type) {
#if CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SIMPLE
	case SAMPLE_RATE_FILTER_SIMPLE:
		if (conversion_ratio == 2) {
			*filter_ptr = filter_48khz_24kz_32bit_simple;
			*filter_size = ARRAY_SIZE(filter_48khz_24kz_32bit_simple);
		} else if (conversion_ratio == 3) {
			*filter_ptr = filter_48khz_16kz_32bit_simple;
			*filter_size = ARRAY_SIZE(filter_48khz_16kz_32bit_simple);
		}
		break;
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SIMPLE */
#if CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SMALL
	case SAMPLE_RATE_FILTER_SMALL:
		if (conversion_ratio == 2) {
			*filter_ptr = filter_48khz_24khz_32bit_small;
			*filter_size = ARRAY_SIZE(filter_48khz_24khz_32bit_small);
		} else if (conversion_ratio == 3) {
			*filter_ptr = filter_48khz_16khz_32bit_small;
			*filter_size = ARRAY_SIZE(filter_48khz_16khz_32bit_small);
		}
		break;
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_FILTER_SMALL */
	default:
		LOG_ERR("No matching filter found\n");
		return -EINVAL;
	}
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32 */
	return 0;
}
