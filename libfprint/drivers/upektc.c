/*
 * UPEK TouchChip driver for libfprint
 * Copyright (C) 2007 Jan-Michael Brummer <buzz2@gmx.de>
 * Copyright (C) 2012 Vasily Khoruzhick <anarsoul@gmail.com>
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

#define FP_COMPONENT "upektc"

#include "drivers_api.h"
#include "upektc.h"

#define UPEKTC_EP_IN (2 | LIBUSB_ENDPOINT_IN)
#define UPEKTC_EP_OUT (3 | LIBUSB_ENDPOINT_OUT)
#define UPEKET_EP_IN (1 | LIBUSB_ENDPOINT_IN)
#define UPEKET_EP_OUT (2 | LIBUSB_ENDPOINT_OUT)
#define BULK_TIMEOUT 4000

struct upektc_dev {
	gboolean deactivating;
	const struct setup_cmd *setup_commands;
	size_t setup_commands_len;
	int ep_in;
	int ep_out;
	int init_idx;
	int sum_threshold;
};

enum upektc_driver_data {
	UPEKTC_2015,
	UPEKTC_3001,
};

static void start_capture(struct fp_img_dev *dev);
static void complete_deactivation(struct fp_img_dev *dev);
static void start_finger_detection(struct fp_img_dev *dev);

/****** INITIALIZATION/DEINITIALIZATION ******/

enum activate_states {
	WRITE_INIT,
	READ_DATA,
	ACTIVATE_NUM_STATES,
};

static void
upektc_next_init_cmd(fpi_ssm           *ssm,
		     struct fp_img_dev *dev)
{
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));

	upekdev->init_idx += 1;
	if (upekdev->init_idx == upekdev->setup_commands_len)
		fpi_ssm_mark_completed(ssm);
	else
		fpi_ssm_jump_to_state(ssm, WRITE_INIT);
}

static void write_init_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));

	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED) &&
		(transfer->length == transfer->actual_length)) {
		if (upekdev->setup_commands[upekdev->init_idx].response_len)
			fpi_ssm_next_state(ssm);
		else
			upektc_next_init_cmd(ssm, dev);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
	libusb_free_transfer(transfer);
}

static void read_init_data_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);

	if (transfer->status == LIBUSB_TRANSFER_COMPLETED)
		upektc_next_init_cmd(ssm, dev);
	else
		fpi_ssm_mark_failed(ssm, -EIO);
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void activate_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(_dev);
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case WRITE_INIT:
	{
		struct libusb_transfer *transfer = fpi_usb_alloc();

		libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), upekdev->ep_out,
			(unsigned char*)upekdev->setup_commands[upekdev->init_idx].cmd,
			UPEKTC_CMD_LEN, write_init_cb, ssm, BULK_TIMEOUT);
		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, -ENOMEM);
		}
	}
	break;
	case READ_DATA:
	{
		struct libusb_transfer *transfer = fpi_usb_alloc();
		unsigned char *data;

		data = g_malloc(upekdev->setup_commands[upekdev->init_idx].response_len);
		libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), upekdev->ep_in, data,
			upekdev->setup_commands[upekdev->init_idx].response_len,
			read_init_data_cb, ssm, BULK_TIMEOUT);

		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(data);
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, r);
		}
	}
	break;
	}
}

static void activate_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	fp_dbg("status %d", fpi_ssm_get_error(ssm));
	fpi_imgdev_activate_complete(dev, fpi_ssm_get_error(ssm));

	if (!fpi_ssm_get_error(ssm))
		start_finger_detection(dev);
	fpi_ssm_free(ssm);
}


/****** FINGER PRESENCE DETECTION ******/

static int finger_present(unsigned char *img, size_t len, int sum_threshold)
{
	int i, sum;

	sum = 0;

	for (i = 0; i < len; i++) {
		if (img[i] < 160) {
			sum++;
		}
	}

	fp_dbg("finger_present: sum is %d\n", sum);
	return sum < sum_threshold ? 0 : 1;
}

static void finger_det_data_cb(struct libusb_transfer *transfer)
{
	struct fp_img_dev *dev = transfer->user_data;
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	unsigned char *data = transfer->buffer;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fp_dbg("data transfer status %d\n", transfer->status);
		fpi_imgdev_session_error(dev, -EIO);
		goto out;
	} else if (transfer->length != transfer->actual_length) {
		fp_dbg("expected %d, got %d bytes", transfer->length,
			transfer->actual_length);
		fpi_imgdev_session_error(dev, -EPROTO);
	}

	if (finger_present(data, IMAGE_SIZE, upekdev->sum_threshold)) {
		/* finger present, start capturing */
		fpi_imgdev_report_finger_status(dev, TRUE);
		start_capture(dev);
	} else {
		/* no finger, poll for a new histogram */
		start_finger_detection(dev);
	}

out:
	g_free(data);
	libusb_free_transfer(transfer);
}

static void finger_det_cmd_cb(struct libusb_transfer *t)
{
	struct libusb_transfer *transfer;
	unsigned char *data;
	int r;
	struct fp_img_dev *dev = t->user_data;
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));

	if (t->status != LIBUSB_TRANSFER_COMPLETED) {
		fp_dbg("req transfer status %d\n", t->status);
		fpi_imgdev_session_error(dev, -EIO);
		goto exit_free_transfer;
	} else if (t->length != t->actual_length) {
		fp_dbg("expected %d, sent %d bytes", t->length, t->actual_length);
		fpi_imgdev_session_error(dev, -EPROTO);
		goto exit_free_transfer;
	}

	transfer = fpi_usb_alloc();
	data = g_malloc(IMAGE_SIZE);
	libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), upekdev->ep_in, data, IMAGE_SIZE,
		finger_det_data_cb, dev, BULK_TIMEOUT);

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(data);
		libusb_free_transfer(transfer);
		fpi_imgdev_session_error(dev, r);
	}
exit_free_transfer:
	libusb_free_transfer(t);
}

static void start_finger_detection(struct fp_img_dev *dev)
{
	int r;
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	struct libusb_transfer *transfer;
	G_DEBUG_HERE();

	if (upekdev->deactivating) {
		complete_deactivation(dev);
		return;
	}

	transfer = fpi_usb_alloc();
	libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), upekdev->ep_out,
		(unsigned char *)scan_cmd, UPEKTC_CMD_LEN,
		finger_det_cmd_cb, dev, BULK_TIMEOUT);
	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		libusb_free_transfer(transfer);
		fpi_imgdev_session_error(dev, r);
	}
}

/****** CAPTURE ******/

enum capture_states {
	CAPTURE_WRITE_CMD,
	CAPTURE_READ_DATA,
	CAPTURE_NUM_STATES,
};

static void capture_cmd_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED) &&
		(transfer->length == transfer->actual_length)) {
		fpi_ssm_next_state(ssm);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
	libusb_free_transfer(transfer);
}

static void capture_read_data_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	unsigned char *data = transfer->buffer;
	struct fp_img *img;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fp_dbg("request is not completed, %d", transfer->status);
		fpi_ssm_mark_failed(ssm, -EIO);
		goto out;
	} else if (transfer->length != transfer->actual_length) {
		fp_dbg("expected %d, sent %d bytes", transfer->length, transfer->actual_length);
		fpi_ssm_mark_failed(ssm, -EPROTO);
		goto out;
	}

	img = fpi_img_new(IMAGE_SIZE);
	memcpy(img->data, data, IMAGE_SIZE);
	fpi_imgdev_image_captured(dev, img);
	fpi_imgdev_report_finger_status(dev, FALSE);
	fpi_ssm_mark_completed(ssm);
out:
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void capture_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(_dev);
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case CAPTURE_WRITE_CMD:
	{
		struct libusb_transfer *transfer = fpi_usb_alloc();

		libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), upekdev->ep_out,
			(unsigned char *)scan_cmd, UPEKTC_CMD_LEN,
			capture_cmd_cb, ssm, BULK_TIMEOUT);
		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, -ENOMEM);
		}
	}
	break;
	case CAPTURE_READ_DATA:
	{
		struct libusb_transfer *transfer = fpi_usb_alloc();
		unsigned char *data;

		data = g_malloc(IMAGE_SIZE);
		libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), upekdev->ep_in, data, IMAGE_SIZE,
			capture_read_data_cb, ssm, BULK_TIMEOUT);

		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(data);
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, r);
		}
	}
	break;
	};
}

static void capture_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(_dev);

	fp_dbg("Capture completed");
	if (upekdev->deactivating)
		complete_deactivation(dev);
	else if (fpi_ssm_get_error(ssm))
		fpi_imgdev_session_error(dev, fpi_ssm_get_error(ssm));
	else
		start_finger_detection(dev);
	fpi_ssm_free(ssm);
}

static void start_capture(struct fp_img_dev *dev)
{
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_ssm *ssm;

	if (upekdev->deactivating) {
		complete_deactivation(dev);
		return;
	}

	ssm = fpi_ssm_new(FP_DEV(dev), capture_run_state, CAPTURE_NUM_STATES, dev);
	G_DEBUG_HERE();
	fpi_ssm_start(ssm, capture_sm_complete);
}

static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_ssm *ssm = fpi_ssm_new(FP_DEV(dev), activate_run_state,
		ACTIVATE_NUM_STATES, dev);
	upekdev->init_idx = 0;
	fpi_ssm_start(ssm, activate_sm_complete);
	return 0;
}

static void dev_deactivate(struct fp_img_dev *dev)
{
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));

	upekdev->deactivating = TRUE;
}

static void complete_deactivation(struct fp_img_dev *dev)
{
	struct upektc_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	G_DEBUG_HERE();

	upekdev->deactivating = FALSE;
	fpi_imgdev_deactivate_complete(dev);
}

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	/* TODO check that device has endpoints we're using */
	int r;
	struct upektc_dev *upekdev;

	r = libusb_claim_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	if (r < 0) {
		fp_err("could not claim interface 0: %s", libusb_error_name(r));
		return r;
	}

	upekdev = g_malloc0(sizeof(struct upektc_dev));
	fp_dev_set_instance_data(FP_DEV(dev), upekdev);
	switch (driver_data) {
	case UPEKTC_2015:
		upekdev->ep_in = UPEKTC_EP_IN;
		upekdev->ep_out = UPEKTC_EP_OUT;
		upekdev->setup_commands = upektc_setup_commands;
		upekdev->setup_commands_len = G_N_ELEMENTS(upektc_setup_commands);
		upekdev->sum_threshold = UPEKTC_SUM_THRESHOLD;
		break;
	case UPEKTC_3001:
		upekdev->ep_in = UPEKET_EP_IN;
		upekdev->ep_out = UPEKET_EP_OUT;
		upekdev->setup_commands = upeket_setup_commands;
		upekdev->setup_commands_len = G_N_ELEMENTS(upeket_setup_commands);
		upekdev->sum_threshold = UPEKET_SUM_THRESHOLD;
		break;
	default:
		fp_err("Device variant %lu is not known\n", driver_data);
		g_free(upekdev);
		fp_dev_set_instance_data(FP_DEV(dev), NULL);
		return -ENODEV;
		break;
	}
	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

static void dev_deinit(struct fp_img_dev *dev)
{
	void *user_data;
	user_data = FP_INSTANCE_DATA(FP_DEV(dev));
	g_free(user_data);
	libusb_release_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	fpi_imgdev_close_complete(dev);
}

static const struct usb_id id_table[] = {
	{ .vendor = 0x0483, .product = 0x2015, .driver_data = UPEKTC_2015 },
	{ .vendor = 0x147e, .product = 0x3001, .driver_data = UPEKTC_3001 },
	{ 0, 0, 0, },
};

struct fp_img_driver upektc_driver = {
	.driver = {
		.id = UPEKTC_ID,
		.name = FP_COMPONENT,
		.full_name = "UPEK TouchChip/Eikon Touch 300",
		.id_table = id_table,
		.scan_type = FP_SCAN_TYPE_PRESS,
	},
	.flags = 0,
	.img_height = IMAGE_HEIGHT,
	.img_width = IMAGE_WIDTH,

	.bz3_threshold = 30,
	.open = dev_init,
	.close = dev_deinit,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
};
