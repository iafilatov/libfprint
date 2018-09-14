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

#include "drivers_api.h"
#include "aeslib.h"
#include "aesx660.h"
#include "aes1660.h"

#define FRAME_WIDTH 128
#define IMAGE_WIDTH	(FRAME_WIDTH + (FRAME_WIDTH / 2))

static struct fpi_frame_asmbl_ctx assembling_ctx = {
	.frame_width = FRAME_WIDTH,
	.frame_height = AESX660_FRAME_HEIGHT,
	.image_width = IMAGE_WIDTH,
	.get_pixel = aes_get_pixel,
};

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	/* TODO check that device has endpoints we're using */
	int r;
	struct aesX660_dev *aesdev;

	r = libusb_claim_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	if (r < 0) {
		fp_err("could not claim interface 0: %s", libusb_error_name(r));
		return r;
	}

	aesdev = g_malloc0(sizeof(struct aesX660_dev));
	fp_dev_set_instance_data(FP_DEV(dev), aesdev);
	aesdev->buffer = g_malloc0(AES1660_FRAME_SIZE + AESX660_HEADER_SIZE);
	aesdev->init_seqs[0] = aes1660_init_1;
	aesdev->init_seqs_len[0] = G_N_ELEMENTS(aes1660_init_1);
	aesdev->init_seqs[1] = aes1660_init_2;
	aesdev->init_seqs_len[1] = G_N_ELEMENTS(aes1660_init_2);
	aesdev->start_imaging_cmd = (unsigned char *)aes1660_start_imaging_cmd;
	aesdev->start_imaging_cmd_len = sizeof(aes1660_start_imaging_cmd);
	aesdev->assembling_ctx = &assembling_ctx;
	aesdev->extra_img_flags = FP_IMG_PARTIAL;

	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

static void dev_deinit(struct fp_img_dev *dev)
{
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	g_free(aesdev->buffer);
	g_free(aesdev);
	libusb_release_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
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
	.bz3_threshold = 20,

	.open = dev_init,
	.close = dev_deinit,
	.activate = aesX660_dev_activate,
	.deactivate = aesX660_dev_deactivate,
};
