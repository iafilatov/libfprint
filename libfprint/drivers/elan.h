/*
 * Elan driver for libfprint
 *
 * Copyright (C) 2017 Igor Filatov <ia.filatov@gmail.com>
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

#ifndef __ELAN_H
#define __ELAN_H

#include <libusb.h>

#define ELAN_VEND_ID 0x04f3

/* a default device type */
#define ELAN_ALL_DEV 0

/* devices with quirks */
#define ELAN_0907 (1 << 0)
#define ELAN_0C03 (1 << 1)

/* devices which don't require frame rotation before assembling */
#define ELAN_NOT_ROTATED ELAN_0C03

/* min FW version that supports calibration */
#define ELAN_MIN_CALIBRATION_FW 0x0138

/* max difference between background image mean and calibration mean
 * (the response value of get_calib_mean_cmd)*/
#define ELAN_CALIBRATION_MAX_DELTA 500

/* times to retry reading calibration status during one session
 * generally prevents calibration from looping indefinitely */
#define ELAN_CALIBRATION_ATTEMPTS 10

/* min and max frames in a capture */
#define ELAN_MIN_FRAMES 7
#define ELAN_MAX_FRAMES 30

/* crop frames to this height to improve stitching */
#define ELAN_MAX_FRAME_HEIGHT 50

/* number of frames to drop at the end of capture because frames captured
 * while the finger is being lifted can be bad */
#define ELAN_SKIP_LAST_FRAMES 1

#define ELAN_CMD_LEN 0x2
#define ELAN_EP_CMD_OUT (0x1 | LIBUSB_ENDPOINT_OUT)
#define ELAN_EP_CMD_IN (0x3 | LIBUSB_ENDPOINT_IN)
#define ELAN_EP_IMG_IN (0x2 | LIBUSB_ENDPOINT_IN)

/* used as response length to tell the driver to skip reading response */
#define ELAN_CMD_SKIP_READ 0

/* usual command timeout and timeout for when we need to check if the finger is
 * still on the device */
#define ELAN_CMD_TIMEOUT 10000
#define ELAN_FINGER_TIMEOUT 200

struct elan_cmd {
	unsigned char cmd[ELAN_CMD_LEN];
	int response_len;
	int response_in;
	unsigned short devices;
};

static const struct elan_cmd get_sensor_dim_cmd = {
	.cmd = {0x00, 0x0c},
	.response_len = 0x4,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEV,
};

static const struct elan_cmd get_fw_ver_cmd = {
	.cmd = {0x40, 0x19},
	.response_len = 0x2,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEV,
};

/* unknown, returns 0x0 0x1 on 0907 */
static const struct elan_cmd activate_cmd_1 = {
	.cmd = {0x40, 0x2a},
	.response_len = 0x2,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_0907,
};

static const struct elan_cmd get_image_cmd = {
	.cmd = {0x00, 0x09},
	/* raw frame sizes are calculated from image dimensions reported by the
	 * device */
	.response_len = -1,
	.response_in = ELAN_EP_IMG_IN,
	.devices = ELAN_ALL_DEV,
};

static const struct elan_cmd read_sensor_status_cmd = {
	.cmd = {0x40, 0x13},
	.response_len = 0x1,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEV,
};

static const struct elan_cmd get_calib_status_cmd = {
	.cmd = {0x40, 0x23},
	.response_len = 0x1,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEV,
};

static const struct elan_cmd get_calib_mean_cmd = {
	.cmd = {0x40, 0x24},
	.response_len = 0x2,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEV,
};

static const struct elan_cmd led_on_cmd = {
	.cmd = {0x40, 0x31},
	.response_len = ELAN_CMD_SKIP_READ,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEV,
};

/* wait for finger
 * subsequent read will not complete until finger is placed on the reader */
static const struct elan_cmd pre_scan_cmd = {
	.cmd = {0x40, 0x3f},
	.response_len = 0x1,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEV,
};

/* led off, stop waiting for finger */
static const struct elan_cmd stop_cmd = {
	.cmd = {0x00, 0x0b},
	.response_len = ELAN_CMD_SKIP_READ,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEV,
};

static const struct usb_id elan_id_table[] = {
	{.vendor = ELAN_VEND_ID,.product = 0x0903,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0907,.driver_data = ELAN_0907},
	{.vendor = ELAN_VEND_ID,.product = 0x0c01,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c02,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c03,.driver_data = ELAN_0C03},
	{.vendor = ELAN_VEND_ID,.product = 0x0c04,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c05,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c06,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c07,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c08,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c09,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c0a,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c0b,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c0c,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c0d,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c0e,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c0f,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c10,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c11,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c12,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c13,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c14,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c15,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c16,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c17,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c18,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c19,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c1a,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c1b,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c1c,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c1d,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c1e,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c1f,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c20,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c21,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c22,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c23,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c24,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c25,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c26,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c27,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c28,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c29,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c2a,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c2b,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c2c,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c2d,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c2e,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c2f,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c30,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c31,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c32,.driver_data = ELAN_ALL_DEV},
	{.vendor = ELAN_VEND_ID,.product = 0x0c33,.driver_data = ELAN_ALL_DEV},
	{0, 0, 0,},
};

static void elan_cmd_done(fpi_ssm *ssm);
static void elan_cmd_read(fpi_ssm *ssm, struct fp_img_dev *dev);

static void elan_calibrate(struct fp_img_dev *dev);
static void elan_capture(struct fp_img_dev *dev);
static void elan_deactivate(struct fp_img_dev *dev);

static int dev_change_state(struct fp_img_dev *dev, enum fp_imgdev_state state);

#endif
