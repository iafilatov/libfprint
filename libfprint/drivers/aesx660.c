/*
 * AuthenTec AES1660/AES2660 common routines
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2007 Cyrille Bagard
 * Copyright (C) 2007-2008,2012 Vasily Khoruzhick
 *
 * Based on AES2550 driver
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

#define FP_COMPONENT "aesX660"

#include "drivers_api.h"
#include "aeslib.h"
#include "aesx660.h"

static void start_capture(struct fp_img_dev *dev);
static void complete_deactivation(struct fp_img_dev *dev);

#define EP_IN			(1 | LIBUSB_ENDPOINT_IN)
#define EP_OUT			(2 | LIBUSB_ENDPOINT_OUT)
#define BULK_TIMEOUT		4000
#define FRAME_HEIGHT		AESX660_FRAME_HEIGHT

#define ID_LEN			8
#define INIT_LEN		4
#define CALIBRATE_DATA_LEN	4
#define FINGER_DET_DATA_LEN	4

static void
aesX660_send_cmd_timeout(fpi_ssm               *ssm,
			 struct fp_dev         *_dev,
			 const unsigned char   *cmd,
			 size_t                 cmd_len,
			 libusb_transfer_cb_fn  callback,
			 int                    timeout)
{
	struct fp_img_dev *dev = FP_IMG_DEV(_dev);
	struct libusb_transfer *transfer = fpi_usb_alloc();
	int r;

	libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), EP_OUT,
		(unsigned char *)cmd, cmd_len,
		callback, ssm, timeout);
	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		fp_dbg("failed to submit transfer\n");
		libusb_free_transfer(transfer);
		fpi_ssm_mark_failed(ssm, -ENOMEM);
	}
}

static void
aesX660_send_cmd(fpi_ssm               *ssm,
		 struct fp_dev         *dev,
		 const unsigned char   *cmd,
		 size_t                 cmd_len,
		 libusb_transfer_cb_fn  callback)
{
	return aesX660_send_cmd_timeout(ssm, dev, cmd, cmd_len, callback, BULK_TIMEOUT);
}

static void
aesX660_read_response(fpi_ssm               *ssm,
		      struct fp_dev         *_dev,
		      size_t                 buf_len,
		      libusb_transfer_cb_fn  callback)
{
	struct fp_img_dev *dev = FP_IMG_DEV(_dev);
	struct libusb_transfer *transfer = fpi_usb_alloc();
	unsigned char *data;
	int r;

	data = g_malloc(buf_len);
	libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), EP_IN,
		data, buf_len,
		callback, ssm, BULK_TIMEOUT);

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		fp_dbg("Failed to submit rx transfer: %d\n", r);
		g_free(data);
		libusb_free_transfer(transfer);
		fpi_ssm_mark_failed(ssm, r);
	}
}

static void aesX660_send_cmd_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED) &&
		(transfer->length == transfer->actual_length)) {
		fpi_ssm_next_state(ssm);
	} else {
		fp_dbg("tx transfer status: %d, actual_len: %.4x\n",
			transfer->status, transfer->actual_length);
		fpi_ssm_mark_failed(ssm, -EIO);
	}
	libusb_free_transfer(transfer);
}

static void aesX660_read_calibrate_data_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	unsigned char *data = transfer->buffer;

	if ((transfer->status != LIBUSB_TRANSFER_COMPLETED) ||
		(transfer->length != transfer->actual_length)) {
		fpi_ssm_mark_failed(ssm, -EIO);
		goto out;
	}
	/* Calibrate response was read correctly? */
	if (data[AESX660_RESPONSE_TYPE_OFFSET] != AESX660_CALIBRATE_RESPONSE) {
		fp_dbg("Bogus calibrate response: %.2x\n", data[0]);
		fpi_ssm_mark_failed(ssm, -EPROTO);
		goto out;
	}

	fpi_ssm_next_state(ssm);
out:
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

/****** FINGER PRESENCE DETECTION ******/

enum finger_det_states {
	FINGER_DET_SEND_LED_CMD,
	FINGER_DET_SEND_FD_CMD,
	FINGER_DET_READ_FD_DATA,
	FINGER_DET_SET_IDLE,
	FINGER_DET_NUM_STATES,
};

static void finger_det_read_fd_data_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	unsigned char *data = transfer->buffer;

	aesdev->fd_data_transfer = NULL;

	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		fp_dbg("Cancelling transfer...\n");
		fpi_ssm_next_state(ssm);
		goto out;
	}

	if ((transfer->status != LIBUSB_TRANSFER_COMPLETED) ||
	   (transfer->length != transfer->actual_length)) {
		fp_dbg("Failed to read FD data\n");
		fpi_ssm_mark_failed(ssm, -EIO);
		goto out;
	}

	if (data[AESX660_RESPONSE_TYPE_OFFSET] != AESX660_FINGER_DET_RESPONSE) {
		fp_dbg("Bogus FD response: %.2x\n", data[0]);
		fpi_ssm_mark_failed(ssm, -EPROTO);
		goto out;
	}

	if (data[AESX660_FINGER_PRESENT_OFFSET] == AESX660_FINGER_PRESENT || aesdev->deactivating) {
		/* Finger present or we're deactivating... */
		fpi_ssm_next_state(ssm);
	} else {
		fp_dbg("Wait for finger returned %.2x as result\n",
			data[AESX660_FINGER_PRESENT_OFFSET]);
		fpi_ssm_jump_to_state(ssm, FINGER_DET_SEND_FD_CMD);
	}
out:
	g_free(data);
	libusb_free_transfer(transfer);
}

static void finger_det_set_idle_cmd_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED) &&
		(transfer->length == transfer->actual_length)) {
		fpi_ssm_mark_completed(ssm);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
	libusb_free_transfer(transfer);
}

static void finger_det_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(_dev);
	int err = fpi_ssm_get_error(ssm);

	fp_dbg("Finger detection completed");
	fpi_imgdev_report_finger_status(dev, TRUE);
	fpi_ssm_free(ssm);

	if (aesdev->deactivating)
		complete_deactivation(dev);
	else if (err)
		fpi_imgdev_session_error(dev, err);
	else {
		fpi_imgdev_report_finger_status(dev, TRUE);
		start_capture(dev);
	}
}

static void finger_det_run_state(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	switch (fpi_ssm_get_cur_state(ssm)) {
	case FINGER_DET_SEND_LED_CMD:
		aesX660_send_cmd(ssm, dev, led_blink_cmd, sizeof(led_blink_cmd),
			aesX660_send_cmd_cb);
	break;
	case FINGER_DET_SEND_FD_CMD:
		aesX660_send_cmd_timeout(ssm, dev, wait_for_finger_cmd, sizeof(wait_for_finger_cmd),
			aesX660_send_cmd_cb, 0);
	break;
	case FINGER_DET_READ_FD_DATA:
		aesX660_read_response(ssm, dev, FINGER_DET_DATA_LEN, finger_det_read_fd_data_cb);
	break;
	case FINGER_DET_SET_IDLE:
		aesX660_send_cmd(ssm, dev, set_idle_cmd, sizeof(set_idle_cmd),
			finger_det_set_idle_cmd_cb);
	break;
	}
}

static void start_finger_detection(struct fp_img_dev *dev)
{
	fpi_ssm *ssm;
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));

	if (aesdev->deactivating) {
		complete_deactivation(dev);
		return;
	}

	ssm = fpi_ssm_new(FP_DEV(dev), finger_det_run_state, FINGER_DET_NUM_STATES, dev);
	fpi_ssm_start(ssm, finger_det_sm_complete);
}

/****** CAPTURE ******/

enum capture_states {
	CAPTURE_SEND_LED_CMD,
	CAPTURE_SEND_CAPTURE_CMD,
	CAPTURE_READ_STRIPE_DATA,
	CAPTURE_SET_IDLE,
	CAPTURE_NUM_STATES,
};

/* Returns number of processed bytes */
static int process_stripe_data(fpi_ssm *ssm, struct fp_img_dev *dev, unsigned char *data)
{
	struct fpi_frame *stripe;
	unsigned char *stripdata;
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));

	stripe = g_malloc(aesdev->assembling_ctx->frame_width * FRAME_HEIGHT / 2 + sizeof(struct fpi_frame)); /* 4 bpp */
	stripdata = stripe->data;

	fp_dbg("Processing frame %.2x %.2x", data[AESX660_IMAGE_OK_OFFSET],
		data[AESX660_LAST_FRAME_OFFSET]);

	stripe->delta_x = (int8_t)data[AESX660_FRAME_DELTA_X_OFFSET];
	stripe->delta_y = -(int8_t)data[AESX660_FRAME_DELTA_Y_OFFSET];
	fp_dbg("Offset to previous frame: %d %d", stripe->delta_x, stripe->delta_y);

	if (data[AESX660_IMAGE_OK_OFFSET] == AESX660_IMAGE_OK) {
		memcpy(stripdata, data + AESX660_IMAGE_OFFSET, aesdev->assembling_ctx->frame_width * FRAME_HEIGHT / 2);

		aesdev->strips = g_slist_prepend(aesdev->strips, stripe);
		aesdev->strips_len++;
		return (data[AESX660_LAST_FRAME_OFFSET] & AESX660_LAST_FRAME_BIT);
	}

	g_free(stripe);
	return 0;
}

static void capture_set_idle_cmd_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));

	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED) &&
		(transfer->length == transfer->actual_length)) {
		struct fp_img *img;

		aesdev->strips = g_slist_reverse(aesdev->strips);
		img = fpi_assemble_frames(aesdev->assembling_ctx, aesdev->strips, aesdev->strips_len);
		img->flags |= aesdev->extra_img_flags;
		g_slist_foreach(aesdev->strips, (GFunc) g_free, NULL);
		g_slist_free(aesdev->strips);
		aesdev->strips = NULL;
		aesdev->strips_len = 0;
		fpi_imgdev_image_captured(dev, img);
		fpi_imgdev_report_finger_status(dev, FALSE);
		fpi_ssm_mark_completed(ssm);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
	libusb_free_transfer(transfer);
}

static void capture_read_stripe_data_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	unsigned char *data = transfer->buffer;
	int finger_missing = 0;
	size_t copied, actual_len = transfer->actual_length;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fpi_ssm_mark_failed(ssm, -EIO);
		goto out;
	}

	fp_dbg("Got %lu bytes of data", actual_len);
	do {
		copied = MIN(aesdev->buffer_max - aesdev->buffer_size, actual_len);
		memcpy(aesdev->buffer + aesdev->buffer_size,
			data,
			copied);
		actual_len -= copied;
		data += copied;
		aesdev->buffer_size += copied;
		fp_dbg("Copied %.4lx bytes into internal buffer",
			copied);
		if (aesdev->buffer_size == aesdev->buffer_max) {
			if (aesdev->buffer_max == AESX660_HEADER_SIZE) {
				aesdev->buffer_max = aesdev->buffer[AESX660_RESPONSE_SIZE_LSB_OFFSET] +
					(aesdev->buffer[AESX660_RESPONSE_SIZE_MSB_OFFSET] << 8) + AESX660_HEADER_SIZE;
				fp_dbg("Got frame, type %.2x size %.4lx",
					aesdev->buffer[AESX660_RESPONSE_TYPE_OFFSET],
					aesdev->buffer_max);
				continue;
			} else {
				finger_missing |= process_stripe_data(ssm, dev, aesdev->buffer);
				aesdev->buffer_max = AESX660_HEADER_SIZE;
				aesdev->buffer_size = 0;
			}
		}
	} while (actual_len);

	fp_dbg("finger %s\n", finger_missing ? "missing" : "present");

	if (finger_missing) {
		fpi_ssm_next_state(ssm);
	} else {
		fpi_ssm_jump_to_state(ssm, CAPTURE_READ_STRIPE_DATA);
	}
out:
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void capture_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(_dev);

	switch (fpi_ssm_get_cur_state(ssm)) {
	case CAPTURE_SEND_LED_CMD:
		aesX660_send_cmd(ssm, _dev, led_solid_cmd, sizeof(led_solid_cmd),
			aesX660_send_cmd_cb);
	break;
	case CAPTURE_SEND_CAPTURE_CMD:
		aesdev->buffer_size = 0;
		aesdev->buffer_max = AESX660_HEADER_SIZE;
		aesX660_send_cmd(ssm, _dev, aesdev->start_imaging_cmd,
			aesdev->start_imaging_cmd_len,
			aesX660_send_cmd_cb);
	break;
	case CAPTURE_READ_STRIPE_DATA:
		aesX660_read_response(ssm, _dev, AESX660_BULK_TRANSFER_SIZE,
			capture_read_stripe_data_cb);
	break;
	case CAPTURE_SET_IDLE:
		fp_dbg("Got %lu frames\n", aesdev->strips_len);
		aesX660_send_cmd(ssm, _dev, set_idle_cmd, sizeof(set_idle_cmd),
			capture_set_idle_cmd_cb);
	break;
	}
}

static void capture_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(_dev);
	int err = fpi_ssm_get_error(ssm);

	fp_dbg("Capture completed");
	fpi_ssm_free(ssm);

	if (aesdev->deactivating)
		complete_deactivation(dev);
	else if (err)
		fpi_imgdev_session_error(dev, err);
	else
		start_finger_detection(dev);
}

static void start_capture(struct fp_img_dev *dev)
{
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_ssm *ssm;

	if (aesdev->deactivating) {
		complete_deactivation(dev);
		return;
	}

	ssm = fpi_ssm_new(FP_DEV(dev), capture_run_state, CAPTURE_NUM_STATES, dev);
	G_DEBUG_HERE();
	fpi_ssm_start(ssm, capture_sm_complete);
}

/****** INITIALIZATION/DEINITIALIZATION ******/

enum activate_states {
	ACTIVATE_SET_IDLE,
	ACTIVATE_SEND_READ_ID_CMD,
	ACTIVATE_READ_ID,
	ACTIVATE_SEND_CALIBRATE_CMD,
	ACTIVATE_READ_CALIBRATE_DATA,
	ACTIVATE_SEND_INIT_CMD,
	ACTIVATE_READ_INIT_RESPONSE,
	ACTIVATE_NUM_STATES,
};

static void activate_read_id_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	unsigned char *data = transfer->buffer;

	if ((transfer->status != LIBUSB_TRANSFER_COMPLETED) ||
		(transfer->length != transfer->actual_length)) {
		fp_dbg("read_id cmd failed\n");
		fpi_ssm_mark_failed(ssm, -EIO);
		goto out;
	}
	/* ID was read correctly */
	if (data[0] == 0x07) {
		fp_dbg("Sensor device id: %.2x%2x, bcdDevice: %.2x.%.2x, init status: %.2x\n",
			data[4], data[3], data[5], data[6], data[7]);
	} else {
		fp_dbg("Bogus read ID response: %.2x\n", data[AESX660_RESPONSE_TYPE_OFFSET]);
		fpi_ssm_mark_failed(ssm, -EPROTO);
		goto out;
	}

	switch (aesdev->init_seq_idx) {
	case 0:
		aesdev->init_seq = aesdev->init_seqs[0];
		aesdev->init_seq_len = aesdev->init_seqs_len[0];
		aesdev->init_seq_idx = 1;
		aesdev->init_cmd_idx = 0;
		/* Do calibration only after 1st init sequence */
		fpi_ssm_jump_to_state(ssm, ACTIVATE_SEND_INIT_CMD);
		break;
	case 1:
		aesdev->init_seq = aesdev->init_seqs[1];
		aesdev->init_seq_len = aesdev->init_seqs_len[1];
		aesdev->init_seq_idx = 2;
		aesdev->init_cmd_idx = 0;
		fpi_ssm_next_state(ssm);
		break;
	default:
		fp_dbg("Failed to init device! init status: %.2x\n", data[7]);
		fpi_ssm_mark_failed(ssm, -EPROTO);
		break;

	}

out:
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void activate_read_init_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	unsigned char *data = transfer->buffer;

	fp_dbg("read_init_cb\n");

	if ((transfer->status != LIBUSB_TRANSFER_COMPLETED) ||
		(transfer->length != transfer->actual_length)) {
		fp_dbg("read_init transfer status: %d, actual_len: %d\n", transfer->status, transfer->actual_length);
		fpi_ssm_mark_failed(ssm, -EIO);
		goto out;
	}
	/* ID was read correctly */
	if (data[0] != 0x42 || data[3] != 0x01) {
		fp_dbg("Bogus read init response: %.2x %.2x\n", data[0],
			data[3]);
		fpi_ssm_mark_failed(ssm, -EPROTO);
		goto out;
	}
	aesdev->init_cmd_idx++;
	if (aesdev->init_cmd_idx == aesdev->init_seq_len) {
		if (aesdev->init_seq_idx < 2)
			fpi_ssm_jump_to_state(ssm, ACTIVATE_SEND_READ_ID_CMD);
		else
			fpi_ssm_mark_completed(ssm);
		goto out;
	}

	fpi_ssm_jump_to_state(ssm, ACTIVATE_SEND_INIT_CMD);
out:
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void activate_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));

	switch (fpi_ssm_get_cur_state(ssm)) {
	case ACTIVATE_SET_IDLE:
		aesdev->init_seq_idx = 0;
		fp_dbg("Activate: set idle\n");
		aesX660_send_cmd(ssm, _dev, set_idle_cmd, sizeof(set_idle_cmd),
			aesX660_send_cmd_cb);
	break;
	case ACTIVATE_SEND_READ_ID_CMD:
		fp_dbg("Activate: read ID\n");
		aesX660_send_cmd(ssm, _dev, read_id_cmd, sizeof(read_id_cmd),
			aesX660_send_cmd_cb);
	break;
	case ACTIVATE_READ_ID:
		aesX660_read_response(ssm, _dev, ID_LEN, activate_read_id_cb);
	break;
	case ACTIVATE_SEND_INIT_CMD:
		fp_dbg("Activate: send init seq #%d cmd #%d\n",
			aesdev->init_seq_idx,
			aesdev->init_cmd_idx);
		aesX660_send_cmd(ssm, _dev,
			aesdev->init_seq[aesdev->init_cmd_idx].cmd,
			aesdev->init_seq[aesdev->init_cmd_idx].len,
			aesX660_send_cmd_cb);
	break;
	case ACTIVATE_READ_INIT_RESPONSE:
		fp_dbg("Activate: read init response\n");
		aesX660_read_response(ssm, _dev, INIT_LEN, activate_read_init_cb);
	break;
	case ACTIVATE_SEND_CALIBRATE_CMD:
		aesX660_send_cmd(ssm, _dev, calibrate_cmd, sizeof(calibrate_cmd),
			aesX660_send_cmd_cb);
	break;
	case ACTIVATE_READ_CALIBRATE_DATA:
		aesX660_read_response(ssm, _dev, CALIBRATE_DATA_LEN, aesX660_read_calibrate_data_cb);
	break;
	}
}

static void activate_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	int err = fpi_ssm_get_error(ssm);
	fp_dbg("status %d", err);
	fpi_imgdev_activate_complete(dev, err);
	fpi_ssm_free(ssm);

	if (!err)
		start_finger_detection(dev);
}

int aesX660_dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	fpi_ssm *ssm = fpi_ssm_new(FP_DEV(dev), activate_run_state,
		ACTIVATE_NUM_STATES, dev);
	fpi_ssm_start(ssm, activate_sm_complete);
	return 0;
}

void aesX660_dev_deactivate(struct fp_img_dev *dev)
{
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));

	if (aesdev->fd_data_transfer)
		libusb_cancel_transfer(aesdev->fd_data_transfer);

	aesdev->deactivating = TRUE;
}

static void complete_deactivation(struct fp_img_dev *dev)
{
	struct aesX660_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	G_DEBUG_HERE();

	aesdev->deactivating = FALSE;
	g_slist_free(aesdev->strips);
	aesdev->strips = NULL;
	aesdev->strips_len = 0;
	fpi_imgdev_deactivate_complete(dev);
}
