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

#include <string.h>
#include <libusb.h>

#define ELAN_VENDOR_ID 0x04f3

/* a default device type */
#define ELAN_ALL_DEVICES 0

/* devices with quirks */
#define ELAN_0907 1

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
#define ELAN_MAX_FRAME_HEIGHT 30

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
	.devices = ELAN_ALL_DEVICES,
};

static const struct elan_cmd get_fw_ver_cmd = {
	.cmd = {0x40, 0x19},
	.response_len = 0x2,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEVICES,
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
	.devices = ELAN_ALL_DEVICES,
};

static const struct elan_cmd read_sensor_status_cmd = {
	.cmd = {0x40, 0x13},
	.response_len = 0x1,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEVICES,
};

static const struct elan_cmd get_calib_status_cmd = {
	.cmd = {0x40, 0x23},
	.response_len = 0x1,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEVICES,
};

static const struct elan_cmd get_calib_mean_cmd = {
	.cmd = {0x40, 0x24},
	.response_len = 0x2,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEVICES,
};

static const struct elan_cmd led_on_cmd = {
	.cmd = {0x40, 0x31},
	.response_len = ELAN_CMD_SKIP_READ,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_0907,
};

/* wait for finger
 * subsequent read will not complete until finger is placed on the reader */
static const struct elan_cmd pre_scan_cmd = {
	.cmd = {0x40, 0x3f},
	.response_len = 0x1,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEVICES,
};

/* led off, stop waiting for finger */
static const struct elan_cmd stop_cmd = {
	.cmd = {0x00, 0x0b},
	.response_len = ELAN_CMD_SKIP_READ,
	.response_in = ELAN_EP_CMD_IN,
	.devices = ELAN_ALL_DEVICES,
};

static void elan_cmd_done(struct fpi_ssm *ssm);
static void elan_cmd_read(struct fpi_ssm *ssm);

static void elan_calibrate(struct fp_img_dev *dev);
static void elan_capture(struct fp_img_dev *dev);
static void elan_deactivate(struct fp_img_dev *dev);

#endif
