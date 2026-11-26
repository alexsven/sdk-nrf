/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "metadata_usb.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/usb/class/usbd_hid.h>

#define METADATA_REPORT_ID 1
#define METADATA_BASE_MAX 249
#define METADATA_REPORT_SIZE (2 + METADATA_BASE_MAX)

BUILD_ASSERT(CONFIG_BT_CTLR_ADV_DATA_LEN_MAX <= METADATA_BASE_MAX + 2);

static const struct net_buf_simple *advertised_base;
static atomic_t base_ready;

static const uint8_t report_descriptor[] = {
	HID_USAGE_PAGE16(0xff00),
	HID_USAGE(1),
	HID_COLLECTION(HID_COLLECTION_APPLICATION),
		HID_REPORT_ID(METADATA_REPORT_ID),
		HID_USAGE(1),
		HID_LOGICAL_MIN8(0),
		HID_LOGICAL_MAX16(0xff, 0x00),
		HID_REPORT_SIZE(8),
		HID_REPORT_COUNT(1 + METADATA_BASE_MAX),
		HID_FEATURE(0x02),
	HID_END_COLLECTION,
};

static int report_size(const struct device *dev, uint8_t type, uint8_t id)
{
	ARG_UNUSED(dev);

	return type == HID_REPORT_TYPE_FEATURE && id == METADATA_REPORT_ID ?
		       METADATA_REPORT_SIZE : -ENOTSUP;
}

static int get_report(const struct device *dev, uint8_t type, uint8_t id, uint16_t len,
		      uint8_t *buf)
{
	if (report_size(dev, type, id) < 0 || len < METADATA_REPORT_SIZE) {
		return -EINVAL;
	}

	memset(buf, 0, METADATA_REPORT_SIZE);
	buf[0] = METADATA_REPORT_ID;
	if (atomic_get(&base_ready)) {
		if (advertised_base->len > METADATA_BASE_MAX) {
			return -EMSGSIZE;
		}
		buf[1] = advertised_base->len;
		memcpy(&buf[2], advertised_base->data, advertised_base->len);
	}
	return METADATA_REPORT_SIZE;
}

static const struct hid_device_ops metadata_ops = {
	.get_report_size = report_size,
	.get_report = get_report,
};

int metadata_usb_init(const struct net_buf_simple *base)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(auracast_metadata));

	if (base == NULL || !device_is_ready(dev)) {
		return -ENODEV;
	}
	advertised_base = base;
	return hid_device_register(dev, report_descriptor, sizeof(report_descriptor), &metadata_ops);
}

void metadata_usb_publish(void)
{
	atomic_set(&base_ready, 1);
}
