/*
 * UPEK TouchChip driver for libfprint
 * Copyright (C) 2013 Vasily Khoruzhick <anarsoul@gmail.com>
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

#define FP_COMPONENT "upektc_img"

#include "drivers_api.h"
#include "upek_proto.h"
#include "aeslib.h"
#include "upektc_img.h"

static void start_capture(struct fp_img_dev *dev);
static void start_deactivation(struct fp_img_dev *dev);

#define EP_IN			(1 | LIBUSB_ENDPOINT_IN)
#define EP_OUT			(2 | LIBUSB_ENDPOINT_OUT)
#define CTRL_TIMEOUT		4000
#define BULK_TIMEOUT		4000

#define IMAGE_WIDTH		144
#define IMAGE_HEIGHT		384
#define IMAGE_SIZE		(IMAGE_WIDTH * IMAGE_HEIGHT)

#define MAX_CMD_SIZE		64
#define MAX_RESPONSE_SIZE	2052
#define SHORT_RESPONSE_SIZE	64

struct upektc_img_dev {
	unsigned char cmd[MAX_CMD_SIZE];
	unsigned char response[MAX_RESPONSE_SIZE];
	unsigned char image_bits[IMAGE_SIZE * 2];
	unsigned char seq;
	size_t image_size;
	size_t response_rest;
	gboolean deactivating;
};

/****** HELPERS ******/

static void upektc_img_cmd_fix_seq(unsigned char *cmd_buf, unsigned char seq)
{
	uint8_t byte;

	byte = cmd_buf[5];
	byte &= 0x0f;
	byte |= (seq << 4);
	cmd_buf[5] = byte;
}

static void upektc_img_cmd_update_crc(unsigned char *cmd_buf, size_t size)
{
	/* CRC does not cover Ciao prefix (4 bytes) and CRC location (2 bytes) */
	uint16_t crc = udf_crc(cmd_buf + 4, size - 6);

	cmd_buf[size - 2] = (crc & 0x00ff);
	cmd_buf[size - 1] = (crc & 0xff00) >> 8;
}

static void
upektc_img_submit_req(fpi_ssm               *ssm,
		      struct fp_img_dev     *dev,
		      const unsigned char   *buf,
		      size_t                 buf_size,
		      unsigned char          seq,
		      libusb_transfer_cb_fn  cb)
{
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	struct libusb_transfer *transfer = fpi_usb_alloc();
	int r;

	BUG_ON(buf_size > MAX_CMD_SIZE);

	transfer->flags |= LIBUSB_TRANSFER_FREE_TRANSFER;

	memcpy(upekdev->cmd, buf, buf_size);
	upektc_img_cmd_fix_seq(upekdev->cmd, seq);
	upektc_img_cmd_update_crc(upekdev->cmd, buf_size);

	libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), EP_OUT, upekdev->cmd, buf_size,
		cb, ssm, BULK_TIMEOUT);

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		libusb_free_transfer(transfer);
		fpi_ssm_mark_failed(ssm, r);
	}
}

static void
upektc_img_read_data(fpi_ssm               *ssm,
		     struct fp_img_dev     *dev,
		     size_t                 buf_size,
		     size_t                 buf_offset,
		     libusb_transfer_cb_fn  cb)
{
	struct libusb_transfer *transfer = fpi_usb_alloc();
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	int r;

	BUG_ON(buf_size > MAX_RESPONSE_SIZE);

	transfer->flags |= LIBUSB_TRANSFER_FREE_TRANSFER;

	libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(FP_DEV(dev)), EP_IN, upekdev->response + buf_offset, buf_size,
		cb, ssm, BULK_TIMEOUT);

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		libusb_free_transfer(transfer);
		fpi_ssm_mark_failed(ssm, r);
	}
}

/****** CAPTURE ******/

enum capture_states {
	CAPTURE_INIT_CAPTURE,
	CAPTURE_READ_DATA,
	CAPTURE_READ_DATA_TERM,
	CAPTURE_ACK_00_28,
	CAPTURE_ACK_08,
	CAPTURE_ACK_FRAME,
	CAPTURE_ACK_00_28_TERM,
	CAPTURE_NUM_STATES,
};

static void capture_reqs_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if ((transfer->status != LIBUSB_TRANSFER_COMPLETED) ||
		(transfer->length != transfer->actual_length)) {
		fpi_ssm_mark_failed(ssm, -EIO);
		return;
	}
	switch (fpi_ssm_get_cur_state(ssm)) {
	case CAPTURE_ACK_00_28_TERM:
		fpi_ssm_jump_to_state(ssm, CAPTURE_READ_DATA_TERM);
		break;
	default:
		fpi_ssm_jump_to_state(ssm, CAPTURE_READ_DATA);
		break;
	}
}

static int upektc_img_process_image_frame(unsigned char *image_buf, unsigned char *cmd_res)
{
	int offset = 8;
	int len = ((cmd_res[5] & 0x0f) << 8) | (cmd_res[6]);

	len -= 1;
	if (cmd_res[7] == 0x2c) {
		len -= 10;
		offset += 10;
	}
	if (cmd_res[7] == 0x20) {
		len -= 4;
	}
	memcpy(image_buf, cmd_res + offset, len);

	return len;
}

static void capture_read_data_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	unsigned char *data = upekdev->response;
	struct fp_img *img;
	size_t response_size;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fp_dbg("request is not completed, %d", transfer->status);
		fpi_ssm_mark_failed(ssm, -EIO);
		return;
	}

	if (upekdev->deactivating) {
		fp_dbg("Deactivate requested\n");
		fpi_ssm_mark_completed(ssm);
		return;
	}

	fp_dbg("request completed, len: %.4x", transfer->actual_length);
	if (transfer->actual_length == 0) {
		fpi_ssm_jump_to_state(ssm, fpi_ssm_get_cur_state(ssm));
		return;
	}

	if (fpi_ssm_get_cur_state(ssm) == CAPTURE_READ_DATA_TERM) {
		fp_dbg("Terminating SSM\n");
		fpi_ssm_mark_completed(ssm);
		return;
	}

	if (!upekdev->response_rest) {
		response_size = ((data[5] & 0x0f) << 8) + data[6];
		response_size += 9; /* 7 bytes for header, 2 for CRC */
		if (response_size > transfer->actual_length) {
			fp_dbg("response_size is %lu, actual_length is %d\n",
				response_size, transfer->actual_length);
			fp_dbg("Waiting for rest of transfer");
			BUG_ON(upekdev->response_rest);
			upekdev->response_rest = response_size - transfer->actual_length;
			fpi_ssm_jump_to_state(ssm, CAPTURE_READ_DATA);
			return;
		}
	}
	upekdev->response_rest = 0;

	switch (data[4]) {
	case 0x00:
		switch (data[7]) {
			/* No finger */
			case 0x28:
				fp_dbg("18th byte is %.2x\n", data[18]);
				switch (data[18]) {
				case 0x0c:
					/* no finger */
					fpi_ssm_jump_to_state(ssm, CAPTURE_ACK_00_28);
					break;
				case 0x00:
					/* finger is present! */
					fpi_ssm_jump_to_state(ssm, CAPTURE_ACK_00_28);
					break;
				case 0x1e:
					/* short scan */
					fp_err("short scan, aborting\n");
					fpi_imgdev_abort_scan(dev, FP_VERIFY_RETRY_TOO_SHORT);
					fpi_imgdev_report_finger_status(dev, FALSE);
					fpi_ssm_jump_to_state(ssm, CAPTURE_ACK_00_28_TERM);
					break;
				case 0x1d:
					/* too much horisontal movement */
					fp_err("too much horisontal movement, aborting\n");
					fpi_imgdev_abort_scan(dev, FP_VERIFY_RETRY_CENTER_FINGER);
					fpi_imgdev_report_finger_status(dev, FALSE);
					fpi_ssm_jump_to_state(ssm, CAPTURE_ACK_00_28_TERM);
					break;
				default:
					/* some error happened, cancel scan */
					fp_err("something bad happened, stop scan\n");
					fpi_imgdev_abort_scan(dev, FP_VERIFY_RETRY);
					fpi_imgdev_report_finger_status(dev, FALSE);
					fpi_ssm_jump_to_state(ssm, CAPTURE_ACK_00_28_TERM);
					break;
				}
				break;
			/* Image frame with additional info */
			case 0x2c:
				fpi_imgdev_report_finger_status(dev, TRUE);
			/* Plain image frame */
			case 0x24:
				upekdev->image_size +=
					upektc_img_process_image_frame(upekdev->image_bits + upekdev->image_size,
						data);
				fpi_ssm_jump_to_state(ssm, CAPTURE_ACK_FRAME);
				break;
			/* Last image frame */
			case 0x20:
				upekdev->image_size +=
					upektc_img_process_image_frame(upekdev->image_bits + upekdev->image_size,
						data);
				BUG_ON(upekdev->image_size != IMAGE_SIZE);
				fp_dbg("Image size is %lu\n", upekdev->image_size);
				img = fpi_img_new(IMAGE_SIZE);
				img->flags = FP_IMG_PARTIAL;
				memcpy(img->data, upekdev->image_bits, IMAGE_SIZE);
				fpi_imgdev_image_captured(dev, img);
				fpi_imgdev_report_finger_status(dev, FALSE);
				fpi_ssm_mark_completed(ssm);
				break;
			default:
				fp_err("Uknown response!\n");
				fpi_ssm_mark_failed(ssm, -EIO);
				break;
		}
		break;
	case 0x08:
		fpi_ssm_jump_to_state(ssm, CAPTURE_ACK_08);
		break;
	default:
		fp_err("Not handled response!\n");
		fpi_ssm_mark_failed(ssm, -EIO);
	}
}

static void capture_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(_dev);

	switch (fpi_ssm_get_cur_state(ssm)) {
	case CAPTURE_INIT_CAPTURE:
		upektc_img_submit_req(ssm, dev, upek2020_init_capture, sizeof(upek2020_init_capture),
			upekdev->seq, capture_reqs_cb);
			upekdev->seq++;
		break;
	case CAPTURE_READ_DATA:
	case CAPTURE_READ_DATA_TERM:
		if (!upekdev->response_rest)
			upektc_img_read_data(ssm, dev, SHORT_RESPONSE_SIZE, 0, capture_read_data_cb);
		else
			upektc_img_read_data(ssm, dev, MAX_RESPONSE_SIZE - SHORT_RESPONSE_SIZE,
			SHORT_RESPONSE_SIZE, capture_read_data_cb);
		break;
	case CAPTURE_ACK_00_28:
	case CAPTURE_ACK_00_28_TERM:
		upektc_img_submit_req(ssm, dev, upek2020_ack_00_28, sizeof(upek2020_ack_00_28),
			upekdev->seq, capture_reqs_cb);
			upekdev->seq++;
		break;
	case CAPTURE_ACK_08:
		upektc_img_submit_req(ssm, dev, upek2020_ack_08, sizeof(upek2020_ack_08),
			0, capture_reqs_cb);
		break;
	case CAPTURE_ACK_FRAME:
		upektc_img_submit_req(ssm, dev, upek2020_ack_frame, sizeof(upek2020_ack_frame),
			upekdev->seq, capture_reqs_cb);
			upekdev->seq++;
		break;
	};
}

static void capture_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(_dev);
	int err = fpi_ssm_get_error(ssm);

	fp_dbg("Capture completed, %d", err);
	fpi_ssm_free(ssm);

	if (upekdev->deactivating)
		start_deactivation(dev);
	else if (err)
		fpi_imgdev_session_error(dev, err);
	else
		start_capture(dev);
}

static void start_capture(struct fp_img_dev *dev)
{
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_ssm *ssm;

	upekdev->image_size = 0;

	ssm = fpi_ssm_new(FP_DEV(dev), capture_run_state, CAPTURE_NUM_STATES, dev);
	fpi_ssm_start(ssm, capture_sm_complete);
}

/****** INITIALIZATION/DEINITIALIZATION ******/

enum deactivate_states {
	DEACTIVATE_DEINIT,
	DEACTIVATE_READ_DEINIT_DATA,
	DEACTIVATE_NUM_STATES,
};

static void deactivate_reqs_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED) &&
		(transfer->length == transfer->actual_length)) {
		fpi_ssm_jump_to_state(ssm, CAPTURE_READ_DATA);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
}

/* TODO: process response properly */
static void deactivate_read_data_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		fpi_ssm_mark_completed(ssm);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
}

static void deactivate_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(_dev);

	switch (fpi_ssm_get_cur_state(ssm)) {
	case DEACTIVATE_DEINIT:
		upektc_img_submit_req(ssm, dev, upek2020_deinit, sizeof(upek2020_deinit),
			upekdev->seq, deactivate_reqs_cb);
		upekdev->seq++;
		break;
	case DEACTIVATE_READ_DEINIT_DATA:
		upektc_img_read_data(ssm, dev, SHORT_RESPONSE_SIZE, 0, deactivate_read_data_cb);
		break;
	};
}

static void deactivate_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(_dev);
	int err = fpi_ssm_get_error(ssm);

	fp_dbg("Deactivate completed");
	fpi_ssm_free(ssm);

	if (err) {
		fpi_imgdev_session_error(dev, err);
		return;
	}

	upekdev->deactivating = FALSE;
	fpi_imgdev_deactivate_complete(dev);
}

static void start_deactivation(struct fp_img_dev *dev)
{
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_ssm *ssm;

	upekdev->image_size = 0;

	ssm = fpi_ssm_new(FP_DEV(dev), deactivate_run_state, DEACTIVATE_NUM_STATES, dev);
	fpi_ssm_start(ssm, deactivate_sm_complete);
}

enum activate_states {
	ACTIVATE_CONTROL_REQ_1,
	ACTIVATE_READ_CTRL_RESP_1,
	ACTIVATE_INIT_1,
	ACTIVATE_READ_INIT_1_RESP,
	ACTIVATE_INIT_2,
	ACTIVATE_READ_INIT_2_RESP,
	ACTIVATE_CONTROL_REQ_2,
	ACTIVATE_READ_CTRL_RESP_2,
	ACTIVATE_INIT_3,
	ACTIVATE_READ_INIT_3_RESP,
	ACTIVATE_INIT_4,
	ACTIVATE_READ_INIT_4_RESP,
	ACTIVATE_NUM_STATES,
};

static void init_reqs_ctrl_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		fpi_ssm_next_state(ssm);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
}

static void init_reqs_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED) &&
		(transfer->length == transfer->actual_length)) {
		fpi_ssm_next_state(ssm);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
}

/* TODO: process response properly */
static void init_read_data_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;

	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		fpi_ssm_next_state(ssm);
	} else {
		fpi_ssm_mark_failed(ssm, -EIO);
	}
}

static void activate_run_state(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	struct libusb_transfer *transfer;
	struct fp_img_dev *idev = user_data;
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(dev);
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case ACTIVATE_CONTROL_REQ_1:
	case ACTIVATE_CONTROL_REQ_2:
	{
		unsigned char *data;

		transfer = fpi_usb_alloc();
		transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER |
			LIBUSB_TRANSFER_FREE_TRANSFER;

		data = g_malloc0(LIBUSB_CONTROL_SETUP_SIZE + 1);
		libusb_fill_control_setup(data,
			LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE, 0x0c, 0x100, 0x0400, 1);
		libusb_fill_control_transfer(transfer, fpi_dev_get_usb_dev(dev), data,
			init_reqs_ctrl_cb, ssm, CTRL_TIMEOUT);
		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(data);
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, r);
		}
	}
	break;
	case ACTIVATE_INIT_1:
		upektc_img_submit_req(ssm, idev, upek2020_init_1, sizeof(upek2020_init_1),
			0, init_reqs_cb);
	break;
	case ACTIVATE_INIT_2:
		upektc_img_submit_req(ssm, idev, upek2020_init_2, sizeof(upek2020_init_2),
			0, init_reqs_cb);
	break;
	case ACTIVATE_INIT_3:
		upektc_img_submit_req(ssm, idev, upek2020_init_3, sizeof(upek2020_init_3),
			0, init_reqs_cb);
	break;
	case ACTIVATE_INIT_4:
		upektc_img_submit_req(ssm, idev, upek2020_init_4, sizeof(upek2020_init_4),
			upekdev->seq, init_reqs_cb);
		/* Seq should be updated after 4th init */
		upekdev->seq++;
	break;
	case ACTIVATE_READ_CTRL_RESP_1:
	case ACTIVATE_READ_CTRL_RESP_2:
	case ACTIVATE_READ_INIT_1_RESP:
	case ACTIVATE_READ_INIT_2_RESP:
	case ACTIVATE_READ_INIT_3_RESP:
	case ACTIVATE_READ_INIT_4_RESP:
		upektc_img_read_data(ssm, idev, SHORT_RESPONSE_SIZE, 0, init_read_data_cb);
	break;
	}
}

static void activate_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	int err = fpi_ssm_get_error(ssm);

	fpi_ssm_free(ssm);
	fp_dbg("%s status %d", __func__, err);
	fpi_imgdev_activate_complete(dev, err);

	if (!err)
		start_capture(dev);
}

static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_ssm *ssm = fpi_ssm_new(FP_DEV(dev), activate_run_state,
		ACTIVATE_NUM_STATES, dev);
	upekdev->seq = 0;
	fpi_ssm_start(ssm, activate_sm_complete);
	return 0;
}

static void dev_deactivate(struct fp_img_dev *dev)
{
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));

	upekdev->deactivating = TRUE;
}

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	/* TODO check that device has endpoints we're using */
	int r;
	struct upektc_img_dev *upekdev;

	r = libusb_claim_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	if (r < 0) {
		fp_err("could not claim interface 0: %s", libusb_error_name(r));
		return r;
	}

	upekdev = g_malloc0(sizeof(struct upektc_img_dev));
	fp_dev_set_instance_data(FP_DEV(dev), upekdev);
	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

static void dev_deinit(struct fp_img_dev *dev)
{
	struct upektc_img_dev *upekdev = FP_INSTANCE_DATA(FP_DEV(dev));
	g_free(upekdev);
	libusb_release_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	fpi_imgdev_close_complete(dev);
}

static int discover(struct libusb_device_descriptor *dsc, uint32_t *devtype)
{
	if (dsc->idProduct == 0x2020 && dsc->bcdDevice == 1)
		return 1;
	if (dsc->idProduct == 0x2016 && dsc->bcdDevice == 2)
		return 1;

	return 0;
}

static const struct usb_id id_table[] = {
	{ .vendor = 0x147e, .product = 0x2016 },
	{ .vendor = 0x147e, .product = 0x2020 },
	{ 0, 0, 0, },
};

struct fp_img_driver upektc_img_driver = {
	.driver = {
		.id = UPEKTC_IMG_ID,
		.name = FP_COMPONENT,
		.full_name = "Upek TouchChip Fingerprint Coprocessor",
		.id_table = id_table,
		.scan_type = FP_SCAN_TYPE_SWIPE,
		.discover = discover,
	},
	.flags = 0,
	.img_height = IMAGE_HEIGHT,
	.img_width = IMAGE_WIDTH,
	.bz3_threshold = 20,

	.open = dev_init,
	.close = dev_deinit,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
};
