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

/*
 * The algorithm which libfprint uses to match fingerprints doesn't like small
 * images like the ones these drivers produce. There's just not enough minutiae
 * (recognizable print-specific points) on them for a reliable match. This means
 * that unless another matching algo is found/implemented, these readers will
 * not work as good with libfprint as they do with vendor drivers.
 *
 * To get bigger images the driver expects you to swipe the finger over the
 * reader. This works quite well for readers with a rectangular 144x64 sensor.
 * Worse than real swipe readers but good enough for day-to-day use. It needs
 * a steady and relatively slow swipe. There are also square 96x96 sensors and
 * I don't know whether they are in fact usable or not because I don't have one.
 * I imagine they'd be less reliable because the resulting image is even
 * smaller. If they can't be made usable with libfprint, I might end up dropping
 * them because it's better than saying they work when they don't.
 */

#define FP_COMPONENT "elan"

#include "drivers_api.h"
#include "elan.h"

#define dbg_buf(buf, len)                                     \
  if (len == 1)                                               \
    fp_dbg("%02x", buf[0]);                                  \
  else if (len == 2)                                          \
    fp_dbg("%04x", buf[0] << 8 | buf[1]);                    \
  else if (len > 2)                                           \
    fp_dbg("%04x... (%d bytes)", buf[0] << 8 | buf[1], len)

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
	unsigned short fw_ver;
	void (*process_frame) (unsigned short *raw_frame, GSList ** frames);
	/* end device config */

	/* commands */
	const struct elan_cmd *cmd;
	int cmd_timeout;
	fpi_usb_transfer *cur_transfer;
	/* end commands */

	/* state */
	enum fp_imgdev_state dev_state;
	enum fp_imgdev_state dev_state_next;
	unsigned char *last_read;
	unsigned char calib_atts_left;
	unsigned char calib_status;
	unsigned short *background;
	unsigned char frame_width;
	unsigned char frame_height;
	unsigned char raw_frame_height;
	int num_frames;
	GSList *frames;
	/* end state */
};

int cmp_short(const void *a, const void *b)
{
	return (int)(*(short *)a - *(short *)b);
}

static void elan_dev_reset(struct elan_dev *elandev)
{
	G_DEBUG_HERE();

	BUG_ON(elandev->cur_transfer);

	elandev->cmd = NULL;
	elandev->cmd_timeout = ELAN_CMD_TIMEOUT;

	elandev->calib_status = 0;

	g_free(elandev->last_read);
	elandev->last_read = NULL;

	g_slist_free_full(elandev->frames, g_free);
	elandev->frames = NULL;
	elandev->num_frames = 0;
}

static void elan_save_frame(struct elan_dev *elandev, unsigned short *frame)
{
	G_DEBUG_HERE();

	/* so far 3 types of readers by sensor dimensions and orientation have been
	 * seen in the wild:
	 * 1. 144x64. Raw images are in portrait orientation while readers themselves
	 *    are placed (e.g. built into a touchpad) in landscape orientation. These
	 *    need to be rotated before assembling.
	 * 2. 96x96 rotated. Like the first type but square. Likewise, need to be
	 *    rotated before assembling.
	 * 3. 96x96 normal. Square and need NOT be rotated. So far there's only been
	 *    1 report of a 0c03 of this type. Hopefully this type can be identified
	 *    by device id (and manufacturers don't just install the readers as they
	 *    please).
	 * we also discard stripes of 'frame_margin' from bottom and top because
	 * assembling works bad for tall frames */

	unsigned char frame_width = elandev->frame_width;
	unsigned char frame_height = elandev->frame_height;
	unsigned char raw_height = elandev->raw_frame_height;
	unsigned char frame_margin = (raw_height - elandev->frame_height) / 2;
	int frame_idx, raw_idx;

	for (int y = 0; y < frame_height; y++)
		for (int x = 0; x < frame_width; x++) {
			if (elandev->dev_type & ELAN_NOT_ROTATED)
				raw_idx = x + (y + frame_margin) * frame_width;
			else
				raw_idx = frame_margin + y + x * raw_height;
			frame_idx = x + y * frame_width;
			frame[frame_idx] =
			    ((unsigned short *)elandev->last_read)[raw_idx];
		}
}

static void elan_save_background(struct elan_dev *elandev)
{
	G_DEBUG_HERE();

	g_free(elandev->background);
	elandev->background =
	    g_malloc(elandev->frame_width * elandev->frame_height *
		     sizeof(short));
	elan_save_frame(elandev, elandev->background);
}

/* save a frame as part of the fingerprint image
 * background needs to have been captured for this routine to work
 * Elantech recommends 2-step non-linear normalization in order to reduce
 * 2^14 ADC resolution to 2^8 image:
 *
 * 1. background is subtracted (done here)
 *
 * 2. pixels are grouped in 3 groups by intensity and each group is mapped
 *    separately onto the normalized frame (done in elan_process_frame_*)
 *    ==== 16383     ____> ======== 255
 *                  /
 *    ----- lvl3 __/
 *                   35% pixels
 *
 *    ----- lvl2 --------> ======== 156
 *
 *                   30% pixels
 *    ----- lvl1 --------> ======== 99
 *
 *                   35% pixels
 *    ----- lvl0 __
 *                 \
 *    ======== 0    \____> ======== 0
 *
 * For some devices we don't do 2. but instead do a simple linear mapping
 * because it seems to produce better results (or at least as good):
 *    ==== 16383      ___> ======== 255
 *                   /
 *    ------ max  __/
 *
 *
 *    ------ min  __
 *                  \
 *    ======== 0     \___> ======== 0
 */
static int elan_save_img_frame(struct elan_dev *elandev)
{
	G_DEBUG_HERE();

	unsigned int frame_size = elandev->frame_width * elandev->frame_height;
	unsigned short *frame = g_malloc(frame_size * sizeof(short));
	elan_save_frame(elandev, frame);
	unsigned int sum = 0;

	for (int i = 0; i < frame_size; i++) {
		if (elandev->background[i] > frame[i])
			frame[i] = 0;
		else
			frame[i] -= elandev->background[i];
		sum += frame[i];
	}

	if (sum == 0) {
		fp_dbg
		    ("frame darker than background; finger present during calibration?");
		return -1;
	}

	elandev->frames = g_slist_prepend(elandev->frames, frame);
	elandev->num_frames += 1;
	return 0;
}

static void elan_process_frame_linear(unsigned short *raw_frame,
				      GSList ** frames)
{
	unsigned int frame_size =
	    assembling_ctx.frame_width * assembling_ctx.frame_height;
	struct fpi_frame *frame =
	    g_malloc(frame_size + sizeof(struct fpi_frame));

	G_DEBUG_HERE();

	unsigned short min = 0xffff, max = 0;
	for (int i = 0; i < frame_size; i++) {
		if (raw_frame[i] < min)
			min = raw_frame[i];
		if (raw_frame[i] > max)
			max = raw_frame[i];
	}

	g_assert(max != min);

	unsigned short px;
	for (int i = 0; i < frame_size; i++) {
		px = raw_frame[i];
		px = (px - min) * 0xff / (max - min);
		frame->data[i] = (unsigned char)px;
	}

	*frames = g_slist_prepend(*frames, frame);
}

static void elan_process_frame_thirds(unsigned short *raw_frame,
				      GSList ** frames)
{
	G_DEBUG_HERE();

	unsigned int frame_size =
	    assembling_ctx.frame_width * assembling_ctx.frame_height;
	struct fpi_frame *frame =
	    g_malloc(frame_size + sizeof(struct fpi_frame));

	unsigned short lvl0, lvl1, lvl2, lvl3;
	unsigned short *sorted = g_malloc(frame_size * sizeof(short));
	memcpy(sorted, raw_frame, frame_size * sizeof(short));
	qsort(sorted, frame_size, sizeof(short), cmp_short);
	lvl0 = sorted[0];
	lvl1 = sorted[frame_size * 3 / 10];
	lvl2 = sorted[frame_size * 65 / 100];
	lvl3 = sorted[frame_size - 1];
	g_free(sorted);

	unsigned short px;
	for (int i = 0; i < frame_size; i++) {
		px = raw_frame[i];
		if (lvl0 <= px && px < lvl1)
			px = (px - lvl0) * 99 / (lvl1 - lvl0);
		else if (lvl1 <= px && px < lvl2)
			px = 99 + ((px - lvl1) * 56 / (lvl2 - lvl1));
		else		// (lvl2 <= px && px <= lvl3)
			px = 155 + ((px - lvl2) * 100 / (lvl3 - lvl2));
		frame->data[i] = (unsigned char)px;
	}

	*frames = g_slist_prepend(*frames, frame);
}

static void elan_submit_image(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));
	GSList *frames = NULL;
	struct fp_img *img;

	G_DEBUG_HERE();

	for (int i = 0; i < ELAN_SKIP_LAST_FRAMES; i++)
		elandev->frames = g_slist_next(elandev->frames);
	elandev->num_frames -= ELAN_SKIP_LAST_FRAMES;

	assembling_ctx.frame_width = elandev->frame_width;
	assembling_ctx.frame_height = elandev->frame_height;
	assembling_ctx.image_width = elandev->frame_width * 3 / 2;
	g_slist_foreach(elandev->frames, (GFunc) elandev->process_frame,
			&frames);
	fpi_do_movement_estimation(&assembling_ctx, frames,
				   elandev->num_frames);
	img = fpi_assemble_frames(&assembling_ctx, frames, elandev->num_frames);

	img->flags |= FP_IMG_PARTIAL;
	fpi_imgdev_image_captured(dev, img);
}

static void elan_cmd_done(fpi_ssm *ssm)
{
	G_DEBUG_HERE();
	fpi_ssm_next_state(ssm);
}

static void elan_cmd_cb(struct libusb_transfer *transfer,
			struct fp_dev          *_dev,
			fpi_ssm                *ssm,
			void                   *user_data)
{
	struct fp_img_dev *dev;
	struct elan_dev *elandev;

	G_DEBUG_HERE();

	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		fp_dbg("transfer cancelled");
		return;
	}

	dev = FP_IMG_DEV(_dev);
	elandev = FP_INSTANCE_DATA(_dev);
	elandev->cur_transfer = NULL;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_COMPLETED:
		if (transfer->length != transfer->actual_length) {
			fp_dbg("transfer length error: expected %d, got %d",
			       transfer->length, transfer->actual_length);
			elan_dev_reset(elandev);
			fpi_ssm_mark_failed(ssm, -EPROTO);
		} else if (transfer->endpoint & LIBUSB_ENDPOINT_IN) {
			/* just finished receiving */
			elandev->last_read = g_memdup(transfer->buffer, transfer->actual_length);
			dbg_buf(transfer->buffer, transfer->actual_length);
			elan_cmd_done(ssm);
		} else {
			/* just finished sending */
			G_DEBUG_HERE();
			elan_cmd_read(ssm, dev);
		}
		break;
	case LIBUSB_TRANSFER_TIMED_OUT:
		fp_dbg("transfer timed out");
		fpi_ssm_mark_failed(ssm, -ETIMEDOUT);
		break;
	default:
		fp_dbg("transfer failed: %d", transfer->status);
		elan_dev_reset(elandev);
		fpi_ssm_mark_failed(ssm, -EIO);
	}
}

static void elan_cmd_read(fpi_ssm *ssm, struct fp_img_dev *dev)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));
	int response_len = elandev->cmd->response_len;
	unsigned char *buffer;

	G_DEBUG_HERE();

	if (elandev->cmd->response_len == ELAN_CMD_SKIP_READ) {
		fp_dbg("skipping read, not expecting anything");
		elan_cmd_done(ssm);
		return;
	}

	if (elandev->cmd->cmd == get_image_cmd.cmd)
		/* raw data has 2-byte "pixels" and the frame is vertical */
		response_len =
		    elandev->raw_frame_height * elandev->frame_width * 2;

	g_clear_pointer(&elandev->last_read, g_free);
	buffer = g_malloc(response_len);

	elandev->cur_transfer = fpi_usb_fill_bulk_transfer(FP_DEV(dev),
							   ssm,
							   elandev->cmd->response_in,
							   buffer,
							   response_len,
							   elan_cmd_cb,
							   NULL,
							   elandev->cmd_timeout);
	int r = fpi_usb_submit_transfer(elandev->cur_transfer);
	if (r < 0)
		fpi_ssm_mark_failed(ssm, r);
}

static void
elan_run_cmd(fpi_ssm               *ssm,
	     struct fp_img_dev     *dev,
	     const struct elan_cmd *cmd,
	     int                    cmd_timeout)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));

	dbg_buf(cmd->cmd, 2);

	elandev->cmd = cmd;
	if (cmd_timeout != -1)
		elandev->cmd_timeout = cmd_timeout;

	if (cmd->devices != ELAN_ALL_DEV && !(cmd->devices & elandev->dev_type)) {
		fp_dbg("skipping for this device");
		elan_cmd_done(ssm);
		return;
	}

	elandev->cur_transfer = fpi_usb_fill_bulk_transfer(FP_DEV(dev),
							   ssm,
							   ELAN_EP_CMD_OUT,
							   g_memdup((char *) cmd->cmd, ELAN_CMD_LEN),
							   ELAN_CMD_LEN,
							   elan_cmd_cb,
							   NULL,
							   elandev->cmd_timeout);
	int r = fpi_usb_submit_transfer(elandev->cur_transfer);
	if (r < 0)
		fpi_ssm_mark_failed(ssm, r);
}

enum stop_capture_states {
	STOP_CAPTURE,
	STOP_CAPTURE_NUM_STATES,
};

static void stop_capture_run_state(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	G_DEBUG_HERE();

	switch (fpi_ssm_get_cur_state(ssm)) {
	case STOP_CAPTURE:
		elan_run_cmd(ssm, FP_IMG_DEV(dev), &stop_cmd, ELAN_CMD_TIMEOUT);
		break;
	}
}

static void stop_capture_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct elan_dev *elandev = FP_INSTANCE_DATA(_dev);
	int error = fpi_ssm_get_error(ssm);

	G_DEBUG_HERE();

	fpi_ssm_free(ssm);

	if (!error) {
		fpi_imgdev_report_finger_status(dev, FALSE);

		/* If verify or identify fails because of short swipe, we need to restart
		 * capture manually. It feels like libfprint or the application should know
		 * better if they want to retry, but they don't. Unless we've been asked to
		 * deactivate, try to re-enter the capture loop. Since state change is
		 * async, there's still a chance to be deactivated by another pending
		 * event. */
		if (elandev->dev_state_next != IMGDEV_STATE_INACTIVE)
			dev_change_state(dev, IMGDEV_STATE_AWAIT_FINGER_ON);

	} else if (error != -ECANCELED)
		fpi_imgdev_abort_scan(dev, error);
}

static void elan_stop_capture(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));

	G_DEBUG_HERE();

	elan_dev_reset(elandev);

	fpi_ssm *ssm =
	    fpi_ssm_new(FP_DEV(dev), stop_capture_run_state,
			STOP_CAPTURE_NUM_STATES, dev);
	fpi_ssm_start(ssm, stop_capture_complete);
}

enum capture_states {
	CAPTURE_LED_ON,
	CAPTURE_WAIT_FINGER,
	CAPTURE_READ_DATA,
	CAPTURE_CHECK_ENOUGH_FRAMES,
	CAPTURE_NUM_STATES,
};

static void capture_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct elan_dev *elandev = FP_INSTANCE_DATA(_dev);
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case CAPTURE_LED_ON:
		elan_run_cmd(ssm, dev, &led_on_cmd, ELAN_CMD_TIMEOUT);
		break;
	case CAPTURE_WAIT_FINGER:
		elan_run_cmd(ssm, dev, &pre_scan_cmd, -1);
		break;
	case CAPTURE_READ_DATA:
		/* 0x55 - finger present
		 * 0xff - device not calibrated (probably) */
		if (elandev->last_read && elandev->last_read[0] == 0x55) {
			if (elandev->dev_state == IMGDEV_STATE_AWAIT_FINGER_ON)
				fpi_imgdev_report_finger_status(dev, TRUE);
			elan_run_cmd(ssm, dev, &get_image_cmd, ELAN_CMD_TIMEOUT);
		} else
			fpi_ssm_mark_failed(ssm, -EBADMSG);
		break;
	case CAPTURE_CHECK_ENOUGH_FRAMES:
		r = elan_save_img_frame(elandev);
		if (r < 0)
			fpi_ssm_mark_failed(ssm, r);
		else if (elandev->num_frames < ELAN_MAX_FRAMES) {
			/* quickly stop if finger is removed */
			elandev->cmd_timeout = ELAN_FINGER_TIMEOUT;
			fpi_ssm_jump_to_state(ssm, CAPTURE_WAIT_FINGER);
		} else {
			fpi_ssm_next_state(ssm);
		}
		break;
	}
}

static void capture_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct elan_dev *elandev = FP_INSTANCE_DATA(_dev);

	G_DEBUG_HERE();

	if (fpi_ssm_get_error(ssm) == -ECANCELED) {
		fpi_ssm_free(ssm);
		return;
	}

	/* either max frames captured or timed out waiting for the next frame */
	if (!fpi_ssm_get_error(ssm)
	    || (fpi_ssm_get_error(ssm) == -ETIMEDOUT
		&& fpi_ssm_get_cur_state(ssm) == CAPTURE_WAIT_FINGER))
		if (elandev->num_frames >= ELAN_MIN_FRAMES)
			elan_submit_image(dev);
		else {
			fp_dbg("swipe too short: want >= %d frames, got %d",
			       ELAN_MIN_FRAMES, elandev->num_frames);
			fpi_imgdev_abort_scan(dev, FP_VERIFY_RETRY_TOO_SHORT);
		}

	/* other error
	 * It says "...abort_scan" but reporting 1 during verification makes it
	 * successful! */
	else
		fpi_imgdev_abort_scan(dev, fpi_ssm_get_error(ssm));

	fpi_ssm_free(ssm);
}

static void elan_capture(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));

	G_DEBUG_HERE();

	elan_dev_reset(elandev);
	fpi_ssm *ssm =
	    fpi_ssm_new(FP_DEV(dev), capture_run_state, CAPTURE_NUM_STATES, dev);
	fpi_ssm_start(ssm, capture_complete);
}

/* this function needs to have elandev->background and elandev->last_read to be
 * the calibration mean */
static int elan_need_calibration(struct elan_dev *elandev)
{
	G_DEBUG_HERE();

	unsigned short calib_mean =
	    elandev->last_read[0] * 0xff + elandev->last_read[1];
	unsigned int bg_mean = 0, delta;
	unsigned int frame_size = elandev->frame_width * elandev->frame_height;

	g_assert(frame_size != 0);

	for (int i = 0; i < frame_size; i++)
		bg_mean += elandev->background[i];
	bg_mean /= frame_size;

	delta =
	    bg_mean > calib_mean ? bg_mean - calib_mean : calib_mean - bg_mean;

	fp_dbg("calibration mean: %d, bg mean: %d, delta: %d", calib_mean,
	       bg_mean, delta);

	return delta > ELAN_CALIBRATION_MAX_DELTA ? 1 : 0;
}

enum calibrate_states {
	CALIBRATE_GET_BACKGROUND,
	CALIBRATE_SAVE_BACKGROUND,
	CALIBRATE_GET_MEAN,
	CALIBRATE_CHECK_NEEDED,
	CALIBRATE_GET_STATUS,
	CALIBRATE_CHECK_STATUS,
	CALIBRATE_REPEAT_STATUS,
	CALIBRATE_NUM_STATES,
};

static void calibrate_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct elan_dev *elandev = FP_INSTANCE_DATA(_dev);

	G_DEBUG_HERE();

	switch (fpi_ssm_get_cur_state(ssm)) {
	case CALIBRATE_GET_BACKGROUND:
		elan_run_cmd(ssm, dev, &get_image_cmd, ELAN_CMD_TIMEOUT);
		break;
	case CALIBRATE_SAVE_BACKGROUND:
		elan_save_background(elandev);
		if (elandev->fw_ver < ELAN_MIN_CALIBRATION_FW) {
			fp_dbg("FW does not support calibration");
			fpi_ssm_mark_completed(ssm);
		} else
			fpi_ssm_next_state(ssm);
		break;
	case CALIBRATE_GET_MEAN:
		elan_run_cmd(ssm, dev, &get_calib_mean_cmd, ELAN_CMD_TIMEOUT);
		break;
	case CALIBRATE_CHECK_NEEDED:
		if (elan_need_calibration(elandev)) {
			elandev->calib_status = 0;
			fpi_ssm_next_state(ssm);
		} else
			fpi_ssm_mark_completed(ssm);
		break;
	case CALIBRATE_GET_STATUS:
		elandev->calib_atts_left -= 1;
		if (elandev->calib_atts_left)
			elan_run_cmd(ssm, dev, &get_calib_status_cmd,
				     ELAN_CMD_TIMEOUT);
		else {
			fp_dbg("calibration failed");
			fpi_ssm_mark_failed(ssm, -1);
		}
		break;
	case CALIBRATE_CHECK_STATUS:
		/* 0x01 - retry, 0x03 - ok
		 * It appears that when reading the response soon after 0x4023 the device
		 * can return 0x03, and only after some time (up to 100 ms) the response
		 * changes to 0x01. It stays that way for some time and then changes back
		 * to 0x03. Because of this we don't just expect 0x03, we want to see 0x01
		 * first. This is to make sure that a full calibration loop has completed */
		fp_dbg("calibration status: 0x%02x", elandev->last_read[0]);
		if (elandev->calib_status == 0x01
		    && elandev->last_read[0] == 0x03) {
			elandev->calib_status = 0x03;
			fpi_ssm_jump_to_state(ssm, CALIBRATE_GET_BACKGROUND);
		} else {
			fpi_timeout *timeout;

			if (elandev->calib_status == 0x00
			    && elandev->last_read[0] == 0x01)
				elandev->calib_status = 0x01;
			timeout = fpi_timeout_add(50, fpi_ssm_next_state_timeout_cb, _dev, ssm);
			fpi_timeout_set_name(timeout, "calibrate_run_state");
		}
		break;
	case CALIBRATE_REPEAT_STATUS:
		fpi_ssm_jump_to_state(ssm, CALIBRATE_GET_STATUS);
		break;
	}
}

static void calibrate_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;

	G_DEBUG_HERE();


	if (fpi_ssm_get_error(ssm) != -ECANCELED)
		elan_capture(dev);

	fpi_ssm_free(ssm);
}

static void elan_calibrate(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));

	G_DEBUG_HERE();

	elan_dev_reset(elandev);
	elandev->calib_atts_left = ELAN_CALIBRATION_ATTEMPTS;

	fpi_ssm *ssm = fpi_ssm_new(FP_DEV(dev), calibrate_run_state,
					  CALIBRATE_NUM_STATES, dev);
	fpi_ssm_start(ssm, calibrate_complete);
}

enum activate_states {
	ACTIVATE_GET_FW_VER,
	ACTIVATE_SET_FW_VER,
	ACTIVATE_GET_SENSOR_DIM,
	ACTIVATE_SET_SENSOR_DIM,
	ACTIVATE_CMD_1,
	ACTIVATE_NUM_STATES,
};

static void activate_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct elan_dev *elandev = FP_INSTANCE_DATA(_dev);

	G_DEBUG_HERE();

	switch (fpi_ssm_get_cur_state(ssm)) {
	case ACTIVATE_GET_FW_VER:
		elan_run_cmd(ssm, dev, &get_fw_ver_cmd, ELAN_CMD_TIMEOUT);
		break;
	case ACTIVATE_SET_FW_VER:
		elandev->fw_ver =
		    (elandev->last_read[0] << 8 | elandev->last_read[1]);
		fp_dbg("FW ver 0x%04hx", elandev->fw_ver);
		fpi_ssm_next_state(ssm);
		break;
	case ACTIVATE_GET_SENSOR_DIM:
		elan_run_cmd(ssm, dev, &get_sensor_dim_cmd, ELAN_CMD_TIMEOUT);
		break;
	case ACTIVATE_SET_SENSOR_DIM:
		/* see elan_save_frame for details */
		if (elandev->dev_type & ELAN_NOT_ROTATED) {
			elandev->frame_width = elandev->last_read[0];
			elandev->frame_height = elandev->raw_frame_height =
			    elandev->last_read[2];
		} else {
			elandev->frame_width = elandev->last_read[2];
			elandev->frame_height = elandev->raw_frame_height =
			    elandev->last_read[0];
		}
		if (elandev->frame_height > ELAN_MAX_FRAME_HEIGHT)
			elandev->frame_height = ELAN_MAX_FRAME_HEIGHT;
		fp_dbg("sensor dimensions, WxH: %dx%d", elandev->frame_width,
		       elandev->raw_frame_height);
		fpi_ssm_next_state(ssm);
		break;
	case ACTIVATE_CMD_1:
		/* TODO: find out what this does, if we need it */
		elan_run_cmd(ssm, dev, &activate_cmd_1, ELAN_CMD_TIMEOUT);
		break;
	}
}

static void activate_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;

	G_DEBUG_HERE();

	if (fpi_ssm_get_error(ssm) != -ECANCELED) {
		fpi_imgdev_activate_complete(dev, fpi_ssm_get_error(ssm));
	}

	fpi_ssm_free(ssm);
}

static void elan_activate(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));

	G_DEBUG_HERE();
	elan_dev_reset(elandev);

	fpi_ssm *ssm =
	    fpi_ssm_new(FP_DEV(dev), activate_run_state, ACTIVATE_NUM_STATES, dev);
	fpi_ssm_start(ssm, activate_complete);
}

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	struct elan_dev *elandev;
	int r;

	G_DEBUG_HERE();

	r = libusb_claim_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	if (r < 0) {
		fp_err("could not claim interface 0: %s", libusb_error_name(r));
		return r;
	}

	elandev = g_malloc0(sizeof(struct elan_dev));
	fp_dev_set_instance_data(FP_DEV(dev), elandev);

	/* common params */
	elandev->dev_type = driver_data;
	elandev->background = NULL;
	elandev->process_frame = elan_process_frame_thirds;

	switch (driver_data) {
	case ELAN_0907:
		elandev->process_frame = elan_process_frame_linear;
		break;
	}

	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

static void elan_deactivate(struct fp_img_dev *dev)
{
	G_DEBUG_HERE();

	fpi_imgdev_deactivate_complete(dev);
}

static void dev_deinit(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));

	G_DEBUG_HERE();

	elan_dev_reset(elandev);
	g_free(elandev->background);
	g_free(elandev);
	libusb_release_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	fpi_imgdev_close_complete(dev);
}

static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	G_DEBUG_HERE();
	elan_activate(dev);
	return 0;
}

static void elan_change_state(struct fp_img_dev *dev)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));
	enum fp_imgdev_state next_state = elandev->dev_state_next;

	if (elandev->dev_state == next_state) {
		fp_dbg("already in %d", next_state);
		return;
	} else
		fp_dbg("changing to %d", next_state);

	switch (next_state) {
	case IMGDEV_STATE_INACTIVE:
		if (elandev->cur_transfer)
			/* deactivation will complete in transfer callback */
			fpi_usb_cancel_transfer(elandev->cur_transfer);
		else
			elan_deactivate(dev);
		break;
	case IMGDEV_STATE_AWAIT_FINGER_ON:
		/* activation completed or another enroll stage started */
		elan_calibrate(dev);
		break;
	case IMGDEV_STATE_CAPTURE:
		/* not used */
		break;
	case IMGDEV_STATE_AWAIT_FINGER_OFF:
		elan_stop_capture(dev);
	}

	elandev->dev_state = next_state;
}

static void
elan_change_state_async(struct fp_dev *dev,
			void          *data)
{
	g_message ("state change dev: %p", dev);
	elan_change_state(FP_IMG_DEV (dev));
}

static int dev_change_state(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	struct elan_dev *elandev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_timeout *timeout;

	G_DEBUG_HERE();

	switch (state) {
	case IMGDEV_STATE_INACTIVE:
	case IMGDEV_STATE_AWAIT_FINGER_ON:
	case IMGDEV_STATE_AWAIT_FINGER_OFF: {
		char *name;

		/* schedule state change instead of calling it directly to allow all actions
		 * related to the previous state to complete */
		elandev->dev_state_next = state;
		timeout = fpi_timeout_add(10, elan_change_state_async, FP_DEV(dev), NULL);

		name = g_strdup_printf ("dev_change_state to %d", state);
		fpi_timeout_set_name(timeout, name);
		g_free (name);

		break;
		}
	case IMGDEV_STATE_CAPTURE:
		/* TODO MAYBE: split capture ssm into smaller ssms and use this state */
		elandev->dev_state = state;
		elandev->dev_state_next = state;
		break;
	default:
		fp_err("unrecognized state %d", state);
		fpi_imgdev_session_error(dev, -EINVAL);
		return -EINVAL;
	}

	/* as of time of writing libfprint never checks the return value */
	return 0;
}

static void dev_deactivate(struct fp_img_dev *dev)
{
	G_DEBUG_HERE();

	dev_change_state(dev, IMGDEV_STATE_INACTIVE);
}

struct fp_img_driver elan_driver = {
	.driver = {
		   .id = ELAN_ID,
		   .name = FP_COMPONENT,
		   .full_name = "ElanTech Fingerprint Sensor",
		   .id_table = elan_id_table,
		   .scan_type = FP_SCAN_TYPE_SWIPE,
		   },
	.flags = 0,

	.bz3_threshold = 24,

	.open = dev_init,
	.close = dev_deinit,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
	.change_state = dev_change_state,
};
