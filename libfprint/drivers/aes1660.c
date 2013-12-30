/*
 * AuthenTec AES1660 driver for libfprint
 * Copyright (C) 2012 Vasily Khoruzhick
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "aes1660"

#include <stdio.h>

#include <errno.h>
#include <string.h>

#include <libusb.h>

#include <fp_internal.h>

#include "aesx660.h"
#include "aes1660.h"
#include "driver_ids.h"

#define FRAME_WIDTH 128
#define SCALE_FACTOR 2

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	/* TODO check that device has endpoints we're using */
	int r;
	struct aesX660_dev *aesdev;

	r = libusb_claim_interface(dev->udev, 0);
	if (r < 0) {
		fp_err("could not claim interface 0");
		return r;
	}

	dev->priv = aesdev = g_malloc0(sizeof(struct aesX660_dev));
	aesdev->buffer = g_malloc0(AES1660_FRAME_SIZE + AESX660_HEADER_SIZE);
	aesdev->init_seqs[0] = aes1660_init_1;
	aesdev->init_seqs_len[0] = array_n_elements(aes1660_init_1);
	aesdev->init_seqs[1] = aes1660_init_2;
	aesdev->init_seqs_len[1] = array_n_elements(aes1660_init_2);
	aesdev->start_imaging_cmd = (unsigned char *)aes1660_start_imaging_cmd;
	aesdev->start_imaging_cmd_len = sizeof(aes1660_start_imaging_cmd);
	aesdev->frame_width = FRAME_WIDTH;

	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

static void dev_deinit(struct fp_img_dev *dev)
{
	struct aesX660_dev *aesdev = dev->priv;
	g_free(aesdev->buffer);
	g_free(aesdev);
	libusb_release_interface(dev->udev, 0);
	fpi_imgdev_close_complete(dev);
}

static const struct usb_id id_table[] = {
	{ .vendor = 0x08ff, .product = 0x1660 },
	{ .vendor = 0x08ff, .product = 0x1680 },
	{ .vendor = 0x08ff, .product = 0x1681 },
	{ .vendor = 0x08ff, .product = 0x1682 },
	{ .vendor = 0x08ff, .product = 0x1683 },
	{ .vendor = 0x08ff, .product = 0x1684 },
	{ .vendor = 0x08ff, .product = 0x1685 },
	{ .vendor = 0x08ff, .product = 0x1686 },
	{ .vendor = 0x08ff, .product = 0x1687 },
	{ .vendor = 0x08ff, .product = 0x1688 },
	{ .vendor = 0x08ff, .product = 0x1689 },
	{ .vendor = 0x08ff, .product = 0x168a },
	{ .vendor = 0x08ff, .product = 0x168b },
	{ .vendor = 0x08ff, .product = 0x168c },
	{ .vendor = 0x08ff, .product = 0x168d },
	{ .vendor = 0x08ff, .product = 0x168e },
	{ .vendor = 0x08ff, .product = 0x168f },
	{ 0, 0, 0, },
};

struct fp_img_driver aes1660_driver = {
	.driver = {
		.id = AES1660_ID,
		.name = FP_COMPONENT,
		.full_name = "AuthenTec AES1660",
		.id_table = id_table,
		.scan_type = FP_SCAN_TYPE_SWIPE,
	},
	.flags = 0,
	.img_height = -1,
	.img_width = FRAME_WIDTH + FRAME_WIDTH / 2,
	.bz3_threshold = 70,

	.open = dev_init,
	.close = dev_deinit,
	.activate = aesX660_dev_activate,
	.deactivate = aesX660_dev_deactivate,
};
