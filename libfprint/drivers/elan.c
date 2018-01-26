/*
 * Elan driver for libfprint
 *
 * Copyright (C) 2017 Igor Filatov <ia.filatov@gmail.com>
 * Copyright (C) 2018 Sébastien Béchet <sebastien.bechet@osinix.com >
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

#define FP_COMPONENT "elan"

#include <errno.h>
#include <libusb.h>
#include <assembling.h>
#include <fp_internal.h>
#include <fprint.h>

#include "elan.h"
#include "driver_ids.h"

unsigned char elan_get_pixel(struct fpi_frame_asmbl_ctx *ctx,
			     struct fpi_frame *frame, unsigned int x,
			     unsigned int y)
{
	return frame->data[x + y * ctx->frame_width];
}

static struct fpi_frame_asmbl_ctx assembling_ctx = {
	.frame_width = 0,
	.frame_height = 0,
	.image_width = 0,
	.get_pixel = elan_get_pixel,
};

struct elan_dev {
	/* device config */
	unsigned short dev_type;
	/* number of pixels to discard on left and right (along raw image height)
	 * because they have different intensity from the rest of the frame */
	unsigned char frame_margin;
	/* end device config */

	/* commands */
	const struct elan_cmd *cmd;
	int cmd_timeout;
	struct libusb_transfer *cur_transfer;
	/* end commands */

	/* state */
	gboolean deactivating;
	unsigned char calib_atts_left;
	unsigned char *last_read;
	unsigned char frame_width;
	unsigned char frame_height;
	unsigned char raw_frame_width;
	int num_frames;
	GSList *frames;
	/* end state */
};

static void elan_dev_reset(struct elan_dev *elandev)
{
	fp_dbg("");

	BUG_ON(elandev->cur_transfer);

	elandev->deactivating = FALSE;

	elandev->cmd = NULL;
	elandev->cmd_timeout = ELAN_CMD_TIMEOUT;

	g_free(elandev->last_read);
	elandev->last_read = NULL;

	g_slist_free_full(elandev->frames, g_free);
	elandev->frames = NULL;
	elandev->num_frames = 0;
}

static void elan_save_frame(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;
	unsigned char raw_height = elandev->frame_width;
	unsigned char raw_width = elandev->raw_frame_width;
	unsigned short *frame =
	    g_malloc(elandev->frame_width * elandev->frame_height * 2);

	fp_dbg("");

	/* Raw images are vertical and perpendicular to swipe direction of a
	 * normalized image, which means we need to make them horizontal before
	 * assembling. We also discard stripes of 'frame_margin' along raw
	 * height. */
	for (int y = 0; y < raw_height; y++)
		for (int x = elandev->frame_margin;
		     x < raw_width - elandev->frame_margin; x++) {
			int frame_idx =
			    y + (x - elandev->frame_margin) * raw_height;
			int raw_idx = x + y * raw_width;
			frame[frame_idx] =
			    ((unsigned short *)elandev->last_read)[raw_idx];
		}

	elandev->frames = g_slist_prepend(elandev->frames, frame);
	elandev->num_frames += 1;
}

/* Transform raw sesnsor data to normalized 8-bit grayscale image. */
static void elan_process_frame(unsigned short *raw_frame, GSList ** frames)
{
	unsigned int frame_size =
	    assembling_ctx.frame_width * assembling_ctx.frame_height;
	struct fpi_frame *frame =
	    g_malloc(frame_size + sizeof(struct fpi_frame));

	fp_dbg("");

	unsigned short min = 0xffff, max = 0;
	for (int i = 0; i < frame_size; i++) {
		if (raw_frame[i] < min)
			min = raw_frame[i];
		if (raw_frame[i] > max)
			max = raw_frame[i];
	}

	unsigned short px;
	for (int i = 0; i < frame_size; i++) {
		px = raw_frame[i];
		if (px <= min)
			px = 0;
		else if (px >= max)
			px = 0xff;
		else
			px = (px - min) * 0xff / (max - min);
		frame->data[i] = (unsigned char)px;
	}

	*frames = g_slist_prepend(*frames, frame);
}

static void elan_submit_image(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;
	GSList *frames = NULL;
	struct fp_img *img;

	fp_dbg("");

	for (int i = 0; i < ELAN_SKIP_LAST_FRAMES; i++)
		elandev->frames = g_slist_next(elandev->frames);

	assembling_ctx.frame_width = elandev->frame_width;
	assembling_ctx.frame_height = elandev->frame_height;
	assembling_ctx.image_width = elandev->frame_width * 3 / 2;
	g_slist_foreach(elandev->frames, (GFunc) elan_process_frame, &frames);
	fpi_do_movement_estimation(&assembling_ctx, frames,
				   elandev->num_frames - ELAN_SKIP_LAST_FRAMES);
	img = fpi_assemble_frames(&assembling_ctx, frames,
				  elandev->num_frames - ELAN_SKIP_LAST_FRAMES);

	img->flags |= FP_IMG_PARTIAL;
	fpi_imgdev_image_captured(dev, img);
}

static void elan_cmd_done(struct fpi_ssm *ssm)
{
	fp_dbg("");
	fpi_ssm_next_state(ssm);
}

static void elan_cmd_cb(struct libusb_transfer *transfer)
{
	struct fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	elandev->cur_transfer = NULL;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_COMPLETED:
		if (transfer->length != transfer->actual_length) {
			fp_dbg("transfer length error: expected %d, got %d",
			       transfer->length, transfer->actual_length);
			elan_dev_reset(elandev);
			fpi_ssm_mark_aborted(ssm, -EPROTO);
		} else if (transfer->endpoint & LIBUSB_ENDPOINT_IN)
			/* just finished receiving */
			elan_cmd_done(ssm);
		else
			/* just finished sending */
			elan_cmd_read(ssm);
		break;
	case LIBUSB_TRANSFER_CANCELLED:
		fp_dbg("transfer cancelled");
		fpi_ssm_mark_aborted(ssm, -ECANCELED);
		break;
	case LIBUSB_TRANSFER_TIMED_OUT:
		fp_dbg("transfer timed out");
		fpi_ssm_mark_aborted(ssm, -ETIMEDOUT);
		break;
	default:
		fp_dbg("transfer failed: %d", transfer->status);
		elan_dev_reset(elandev);
		fpi_ssm_mark_aborted(ssm, -EIO);
	}
}

static void elan_cmd_read(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;
	int response_len = elandev->cmd->response_len;

	fp_dbg("");

	if (!(elandev->cmd->response_len)) {
		fp_dbg("skipping read, not expecting anything");
		elan_cmd_done(ssm);
		return;
	}

	if (elandev->cmd->cmd == get_image_cmd.cmd)
		/* raw data has 2-byte "pixels" and the frame is vertical */
		response_len =
		    elandev->raw_frame_width * elandev->frame_width * 2;

	struct libusb_transfer *transfer = libusb_alloc_transfer(0);
	if (!transfer) {
		fpi_ssm_mark_aborted(ssm, -ENOMEM);
		return;
	}
	elandev->cur_transfer = transfer;

	g_free(elandev->last_read);
	elandev->last_read = g_malloc(response_len);

	libusb_fill_bulk_transfer(transfer, dev->udev,
				  elandev->cmd->response_in, elandev->last_read,
				  response_len, elan_cmd_cb, ssm,
				  elandev->cmd_timeout);
	transfer->flags = LIBUSB_TRANSFER_FREE_TRANSFER;
	int r = libusb_submit_transfer(transfer);
	if (r < 0)
		fpi_ssm_mark_aborted(ssm, r);
}

static void elan_run_cmd(struct fpi_ssm *ssm, const struct elan_cmd *cmd,
			 int cmd_timeout)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	fp_dbg("%02x%02x", cmd->cmd[0], cmd->cmd[1]);

	elandev->cmd = cmd;
	if (cmd_timeout != -1)
		elandev->cmd_timeout = cmd_timeout;

	if (!(cmd->devices & elandev->dev_type)) {
		fp_dbg("skipping for this device");
		elan_cmd_done(ssm);
		return;
	}

	struct libusb_transfer *transfer = libusb_alloc_transfer(0);
	if (!transfer) {
		fpi_ssm_mark_aborted(ssm, -ENOMEM);
		return;
	}
	elandev->cur_transfer = transfer;

	libusb_fill_bulk_transfer(transfer, dev->udev, ELAN_EP_CMD_OUT,
				  cmd->cmd, ELAN_CMD_LEN, elan_cmd_cb, ssm,
				  elandev->cmd_timeout);
	transfer->flags = LIBUSB_TRANSFER_FREE_TRANSFER;
	int r = libusb_submit_transfer(transfer);
	if (r < 0)
		fpi_ssm_mark_aborted(ssm, r);
}

enum deactivate_states {
	DEACTIVATE,
	DEACTIVATE_NUM_STATES,
};

static void deactivate_run_state(struct fpi_ssm *ssm)
{
	fp_dbg("");

	switch (ssm->cur_state) {
	case DEACTIVATE:
		elan_run_cmd(ssm, &stop_cmd, ELAN_CMD_TIMEOUT);
		break;
	}
}

static void deactivate_complete(struct fpi_ssm *ssm)
{
	fp_dbg("");

	struct fp_img_dev *dev = ssm->priv;

	fpi_imgdev_deactivate_complete(dev);
}

static void elan_deactivate(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	elan_dev_reset(elandev);

	struct fpi_ssm *ssm =
	    fpi_ssm_new(dev->dev, deactivate_run_state, DEACTIVATE_NUM_STATES);
	ssm->priv = dev;
	fpi_ssm_start(ssm, deactivate_complete);
}

enum capture_states {
	CAPTURE_LED_ON,
	CAPTURE_WAIT_FINGER,
	CAPTURE_READ_DATA,
	CAPTURE_CHECK_ENOUGH_FRAMES,
	CAPTURE_NUM_STATES,
};

static void capture_run_state(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	switch (ssm->cur_state) {
	case CAPTURE_LED_ON:
		elan_run_cmd(ssm, &led_on_cmd, ELAN_CMD_TIMEOUT);
		break;
	case CAPTURE_WAIT_FINGER:
		elan_run_cmd(ssm, &pre_scan_cmd, -1);
		break;
	case CAPTURE_READ_DATA:
		/* 0x55 - finger present
		 * 0xff - device not calibrated (probably) */
		if (elandev->last_read && elandev->last_read[0] == 0x55) {
			fpi_imgdev_report_finger_status(dev, TRUE);
			elan_run_cmd(ssm, &get_image_cmd, ELAN_CMD_TIMEOUT);
		} else
			fpi_ssm_mark_aborted(ssm, FP_VERIFY_RETRY);
		break;
	case CAPTURE_CHECK_ENOUGH_FRAMES:
		elan_save_frame(dev);
		if (elandev->num_frames < ELAN_MAX_FRAMES) {
			/* quickly stop if finger is removed */
			elandev->cmd_timeout = ELAN_FINGER_TIMEOUT;
			fpi_ssm_jump_to_state(ssm, CAPTURE_WAIT_FINGER);
		} else {
			fpi_ssm_next_state(ssm);
		}
		break;
	}
}

static void elan_capture_async(void *data)
{
	elan_capture((struct fp_img_dev *)data);
}

static void capture_complete(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	if (elandev->deactivating)
		elan_deactivate(dev);

	/* either max frames captured or timed out waiting for the next frame */
	else if (!ssm->error
		 || (ssm->error == -ETIMEDOUT
		     && ssm->cur_state == CAPTURE_WAIT_FINGER))
		if (elandev->num_frames >= ELAN_MIN_FRAMES) {
			elan_submit_image(dev);
			fpi_imgdev_report_finger_status(dev, FALSE);
		} else
			fpi_imgdev_session_error(dev,
						 FP_VERIFY_RETRY_TOO_SHORT);

	/* other error
	 * It says "...session_error" but repotring 1 during verification
	 * makes it successful! */
	else
		fpi_imgdev_session_error(dev, FP_VERIFY_NO_MATCH);

	/* When enrolling the lib won't restart the capture after a stage has
	 * completed, so we need to keep feeding it images till it's had enough.
	 * But after that it can't finalize enrollemnt until this callback exits.
	 * That's why we schedule elan_capture instead of running it directly. */
	if (dev->dev->state == DEV_STATE_ENROLLING
	    && !fpi_timeout_add(10, elan_capture_async, dev))
		fpi_imgdev_session_error(dev, -ETIME);

	fpi_ssm_free(ssm);
}

static void elan_capture(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	elan_dev_reset(elandev);
	struct fpi_ssm *ssm =
	    fpi_ssm_new(dev->dev, capture_run_state, CAPTURE_NUM_STATES);
	ssm->priv = dev;
	fpi_ssm_start(ssm, capture_complete);
}

static void fpi_ssm_next_state_async(void *data)
{
	fpi_ssm_next_state((struct fpi_ssm *)data);
}

/* this function needs last_read to be the calibration mean and at least
 * one frame */
static int elan_need_calibration(struct elan_dev *elandev)
{
	fp_dbg("");

	if (elandev->dev_type & ELAN_0903) {
		fp_dbg("don't know how to calibrate this device");
		return 0;
	}

	unsigned short calib_mean =
	    elandev->last_read[0] * 0xff + elandev->last_read[1];
	unsigned short *bg_data = ((GSList *) elandev->frames)->data;
	unsigned int bg_mean = 0, delta;
	unsigned int frame_size = elandev->frame_width * elandev->frame_height;

	for (int i = 0; i < frame_size; i++)
		bg_mean += bg_data[i];
	bg_mean /= frame_size;

	delta =
	    bg_mean > calib_mean ? bg_mean - calib_mean : calib_mean - bg_mean;

	fp_dbg("calibration mean: %d, bg mean: %d, delta: %d", calib_mean,
	       bg_mean, delta);

	return delta > ELAN_CALIBRATION_MAX_DELTA ? 1 : 0;
}

enum calibrate_states {
	CALIBRATE_START,
	CALIBRATE_CHECK_RESULT,
	CALIBRATE_REPEAT,
	CALIBRATE_GET_BACKGROUND,
	CALIBRATE_SAVE_BACKGROUND,
	CALIBRATE_GET_MEAN,
	CALIBRATE_NUM_STATES,
};

static void calibrate_run_state(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	switch (ssm->cur_state) {
	case CALIBRATE_START:
		elandev->calib_atts_left -= 1;
		if (elandev->calib_atts_left)
			elan_run_cmd(ssm, &run_calibration_cmd,
				     ELAN_CMD_TIMEOUT);
		else {
			fp_dbg("too many calibration attempts");
			fpi_ssm_mark_aborted(ssm, -1);
		}
		break;
	case CALIBRATE_CHECK_RESULT:
		/* 0x01 - retry, 0x03 - ok
		 * but some devices send other responses so in order to avoid needless
		 * retries we don't check 0x3 but only retry on 0x1 (need to wait 50 ms) */
		fp_dbg("calibration status: 0x%02x", elandev->last_read[0]);
		if (elandev->last_read[0] == 0x01) {
			if (!fpi_timeout_add(50, fpi_ssm_next_state_async, ssm))
				fpi_ssm_mark_aborted(ssm, -ETIME);
		} else
			fpi_ssm_jump_to_state(ssm, CALIBRATE_GET_BACKGROUND);
		break;
	case CALIBRATE_REPEAT:
		fpi_ssm_jump_to_state(ssm, CALIBRATE_START);
		break;
	case CALIBRATE_GET_BACKGROUND:
		elan_run_cmd(ssm, &get_image_cmd, ELAN_CMD_TIMEOUT);
		break;
	case CALIBRATE_SAVE_BACKGROUND:
		elan_save_frame(dev);
		fpi_ssm_next_state(ssm);
		break;
	case CALIBRATE_GET_MEAN:
		elan_run_cmd(ssm, &get_calib_mean_cmd, ELAN_CMD_TIMEOUT);
		break;
	}
}

static void calibrate_complete(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	if (elandev->deactivating)
		elan_deactivate(dev);
	else if (ssm->error)
		fpi_imgdev_activate_complete(dev, ssm->error);
	else if (elan_need_calibration(elandev))
		elan_calibrate(dev);
	else {
		fpi_imgdev_activate_complete(dev, 0);
		elan_capture(dev);
	}

	fpi_ssm_free(ssm);
}

static void elan_calibrate(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	elan_dev_reset(elandev);
	struct fpi_ssm *ssm =
	    fpi_ssm_new(dev->dev, calibrate_run_state, CALIBRATE_NUM_STATES);
	ssm->priv = dev;
	fpi_ssm_start(ssm, calibrate_complete);
}

enum activate_states {
	ACTIVATE_GET_FW_VER,
	ACTIVATE_PRINT_FW_VER,
	ACTIVATE_GET_SENSOR_DIM,
	ACTIVATE_SET_SENSOR_DIM,
	ACTIVATE_CMD_1,
	ACTIVATE_GET_BACKGROUND,
	ACTIVATE_SAVE_BACKGROUND,
	ACTIVATE_GET_MEAN,
	ACTIVATE_NUM_STATES,
};

static void activate_run_state(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	switch (ssm->cur_state) {
	case ACTIVATE_GET_FW_VER:
		elan_run_cmd(ssm, &get_fw_ver_cmd, ELAN_CMD_TIMEOUT);
		break;
	case ACTIVATE_PRINT_FW_VER:
		fp_dbg("FW ver %d.%d", elandev->last_read[0],
		       elandev->last_read[1]);
		fpi_ssm_next_state(ssm);
		break;
	case ACTIVATE_GET_SENSOR_DIM:
		elan_run_cmd(ssm, &get_sensor_dim_cmd, ELAN_CMD_TIMEOUT);
		break;
	case ACTIVATE_SET_SENSOR_DIM:
		elandev->frame_width = elandev->last_read[2];
		elandev->raw_frame_width = elandev->last_read[0];
		elandev->frame_height =
		    elandev->raw_frame_width - 2 * elandev->frame_margin;
		/* see elan_save_frame */
		fp_dbg("sensor dimensions, WxH: %dx%d", elandev->frame_width,
		       elandev->raw_frame_width);
		fpi_ssm_next_state(ssm);
		break;
	case ACTIVATE_CMD_1:
		/* TODO: find out what this does, if we need it */
		elan_run_cmd(ssm, &activate_cmd_1, ELAN_CMD_TIMEOUT);
		break;
	case ACTIVATE_GET_BACKGROUND:
		elan_run_cmd(ssm, &get_image_cmd, ELAN_CMD_TIMEOUT);
		break;
	case ACTIVATE_SAVE_BACKGROUND:
		elan_save_frame(dev);
		fpi_ssm_next_state(ssm);
		break;
	case ACTIVATE_GET_MEAN:
		elan_run_cmd(ssm, &get_calib_mean_cmd, ELAN_CMD_TIMEOUT);
		break;
	}
}

static void activate_complete(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	if (elandev->deactivating)
		elan_deactivate(dev);
	else if (ssm->error)
		fpi_imgdev_activate_complete(dev, ssm->error);
	else if (elan_need_calibration(elandev))
		elan_calibrate(dev);
	else {
		fpi_imgdev_activate_complete(dev, 0);
		elan_capture(dev);
	}
	fpi_ssm_free(ssm);
}

static void elan_activate(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	elan_dev_reset(elandev);
	struct fpi_ssm *ssm =
	    fpi_ssm_new(dev->dev, activate_run_state, ACTIVATE_NUM_STATES);
	ssm->priv = dev;
	fpi_ssm_start(ssm, activate_complete);
}

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	struct elan_dev *elandev;
	int r;

	fp_dbg("");

	r = libusb_claim_interface(dev->udev, 0);
	if (r < 0) {
		fp_err("could not claim interface 0: %s", libusb_error_name(r));
		return r;
	}

	dev->priv = elandev = g_malloc0(sizeof(struct elan_dev));

	/* common params */
	elandev->dev_type = driver_data;
	elandev->calib_atts_left = ELAN_CALIBRATION_ATTEMPTS;
	elandev->frame_margin = 0;

	switch (driver_data) {
	case ELAN_0907:
		elandev->frame_margin = 12;
		break;
	}

	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

enum reset_sensor_states {
	RESET_SENSOR_DO_RESET,
	RESET_SENSOR_WAIT,
	RESET_SENSOR_FUSE_LOAD,
	RESET_SENSOR_STATUS,
	RESET_SENSOR_NUM_STATES,
};

static void reset_sensor_run_state(struct fpi_ssm *ssm)
{
	switch (ssm->cur_state) {
	case RESET_SENSOR_DO_RESET:
		elan_run_cmd(ssm, &reset_sensor_cmd, ELAN_CMD_TIMEOUT);
		break;
	case RESET_SENSOR_WAIT:
		/* must wait 5 ms after sensor reset command */
		if (!fpi_timeout_add(5, fpi_ssm_next_state_async, ssm))
			fpi_ssm_mark_aborted(ssm, -ETIME);
		break;
	case RESET_SENSOR_FUSE_LOAD:
		elan_run_cmd(ssm, &fuse_load_cmd, ELAN_CMD_TIMEOUT);
		break;
	case RESET_SENSOR_STATUS:
		elan_run_cmd(ssm, &read_sensor_status_cmd, ELAN_CMD_TIMEOUT);
		break;
	}
}

static void reset_sensor_complete(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	if (elandev->deactivating)
		elan_deactivate(dev);
	else if (ssm->error)
		fpi_imgdev_activate_complete(dev, ssm->error);
	else
		elan_activate(dev);

	fpi_ssm_free(ssm);
}

static void elan_reset_sensor(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	elan_dev_reset(elandev);
	struct fpi_ssm *ssm = fpi_ssm_new(dev->dev, reset_sensor_run_state,
					  RESET_SENSOR_NUM_STATES);
	ssm->priv = dev;
	fpi_ssm_start(ssm, reset_sensor_complete);
}

static void dev_deinit(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;

	fp_dbg("");

	elan_dev_reset(elandev);
	g_free(elandev);
	libusb_release_interface(dev->udev, 0);
	fpi_imgdev_close_complete(dev);
}

static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	elan_reset_sensor(dev);
	return 0;
}

static void dev_deactivate(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = dev->priv;

	elandev->deactivating = TRUE;

	if (elandev->cur_transfer)
		libusb_cancel_transfer(elandev->cur_transfer);
	else
		elan_deactivate(dev);
}

static const struct usb_id id_table[] = {
	{.vendor = ELAN_VENDOR_ID,.product = 0x0903,.driver_data = ELAN_0903},
	{.vendor = ELAN_VENDOR_ID,.product = 0x0907,.driver_data = ELAN_0907},
	{.vendor = ELAN_VENDOR_ID,.product = 0x0c03,.driver_data = ELAN_0C03},
	{.vendor = ELAN_VENDOR_ID,.product = 0x0c16,.driver_data = ELAN_0C16},
	{.vendor = ELAN_VENDOR_ID,.product = 0x0c1a,.driver_data = ELAN_0C1A},
	{.vendor = ELAN_VENDOR_ID,.product = 0x0c26,.driver_data = ELAN_0C26},
	{0, 0, 0,},
};

struct fp_img_driver elan_driver = {
	.driver = {
		   .id = ELAN_ID,
		   .name = FP_COMPONENT,
		   .full_name = "ElanTech Fingerprint Sensor",
		   .id_table = id_table,
		   .scan_type = FP_SCAN_TYPE_SWIPE,
		   },
	.flags = 0,

	.bz3_threshold = 22,

	.open = dev_init,
	.close = dev_deinit,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
};
