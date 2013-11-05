/*
 * Validity Sensors, Inc. VFS5011 Fingerprint Reader driver for libfprint
 * Copyright (C) 2013 Arseniy Lartsev <arseniy@chalmers.se>
 *                    AceLan Kao <acelan.kao@canonical.com>
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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <libusb.h>
#include <fp_internal.h>
#include "driver_ids.h"

#include "vfs5011_proto.h"

/* =================== sync/async USB transfer sequence ==================== */

enum {
	ACTION_SEND,
	ACTION_RECEIVE,
};

struct usb_action {
	int type;
	const char *name;
	int endpoint;
	int size;
	unsigned char *data;
	int correct_reply_size;
};

#define SEND(ENDPOINT, COMMAND) \
{ \
	.type = ACTION_SEND, \
	.endpoint = ENDPOINT, \
	.name = #COMMAND, \
	.size = sizeof(COMMAND), \
	.data = COMMAND \
},

#define RECV(ENDPOINT, SIZE) \
{ \
	.type = ACTION_RECEIVE, \
	.endpoint = ENDPOINT, \
	.size = SIZE, \
	.data = NULL \
},

#define RECV_CHECK(ENDPOINT, SIZE, EXPECTED) \
{ \
	.type = ACTION_RECEIVE, \
	.endpoint = ENDPOINT, \
	.size = SIZE, \
	.data = EXPECTED, \
	.correct_reply_size = sizeof(EXPECTED) \
},

struct usbexchange_data {
	int stepcount;
	struct fp_img_dev *device;
	struct usb_action *actions;
	void *receive_buf;
	int timeout;
};

static void async_send_cb(struct libusb_transfer *transfer)
{
	struct fpi_ssm *ssm = transfer->user_data;
	struct usbexchange_data *data = (struct usbexchange_data *)ssm->priv;
	struct usb_action *action;

	if (ssm->cur_state >= data->stepcount) {
		fp_err("Radiation detected!");
		fpi_imgdev_session_error(data->device, -EINVAL);
		fpi_ssm_mark_aborted(ssm, -EINVAL);
		goto out;
	}

	action = &data->actions[ssm->cur_state];
	if (action->type != ACTION_SEND) {
		fp_err("Radiation detected!");
		fpi_imgdev_session_error(data->device, -EINVAL);
		fpi_ssm_mark_aborted(ssm, -EINVAL);
		goto out;
	}

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		/* Transfer not completed, return IO error */
		fp_err("transfer not completed, status = %d", transfer->status);
		fpi_imgdev_session_error(data->device, -EIO);
		fpi_ssm_mark_aborted(ssm, -EIO);
		goto out;
	}
	if (transfer->length != transfer->actual_length) {
		/* Data sended mismatch with expected, return protocol error */
		fp_err("length mismatch, got %d, expected %d",
			transfer->actual_length, transfer->length);
		fpi_imgdev_session_error(data->device, -EIO);
		fpi_ssm_mark_aborted(ssm, -EIO);
		goto out;
	}

	/* success */
	fpi_ssm_next_state(ssm);

out:
	libusb_free_transfer(transfer);
}

static void async_recv_cb(struct libusb_transfer *transfer)
{
	struct fpi_ssm *ssm = transfer->user_data;
	struct usbexchange_data *data = (struct usbexchange_data *)ssm->priv;
	struct usb_action *action;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		/* Transfer not completed, return IO error */
		fp_err("transfer not completed, status = %d", transfer->status);
		fpi_imgdev_session_error(data->device, -EIO);
		fpi_ssm_mark_aborted(ssm, -EIO);
		goto out;
	}

	if (ssm->cur_state >= data->stepcount) {
		fp_err("Radiation detected!");
		fpi_imgdev_session_error(data->device, -EINVAL);
		fpi_ssm_mark_aborted(ssm, -EINVAL);
		goto out;
	}

	action = &data->actions[ssm->cur_state];
	if (action->type != ACTION_RECEIVE) {
		fp_err("Radiation detected!");
		fpi_imgdev_session_error(data->device, -EINVAL);
		fpi_ssm_mark_aborted(ssm, -EINVAL);
		goto out;
	}

	if (action->data != NULL) {
		if (transfer->actual_length != action->correct_reply_size) {
			fp_err("Got %d bytes instead of %d",
				transfer->actual_length,
				action->correct_reply_size);
			fpi_imgdev_session_error(data->device, -EIO);
			fpi_ssm_mark_aborted(ssm, -EIO);
			goto out;
		}
		if (memcmp(transfer->buffer, action->data,
					action->correct_reply_size) != 0) {
			fp_dbg("Wrong reply:");
			fpi_imgdev_session_error(data->device, -EIO);
			fpi_ssm_mark_aborted(ssm, -EIO);
			goto out;
		}
	} else
		fp_dbg("Got %d bytes out of %d", transfer->actual_length,
		       transfer->length);

	fpi_ssm_next_state(ssm);
out:
	libusb_free_transfer(transfer);
}

static void usbexchange_loop(struct fpi_ssm *ssm)
{
	struct usbexchange_data *data = (struct usbexchange_data *)ssm->priv;
	if (ssm->cur_state >= data->stepcount) {
		fp_err("Bug detected: state %d out of range, only %d steps",
				ssm->cur_state, data->stepcount);
		fpi_imgdev_session_error(data->device, -EINVAL);
		fpi_ssm_mark_aborted(ssm, -EINVAL);
		return;
	}

	struct usb_action *action = &data->actions[ssm->cur_state];
	struct libusb_transfer *transfer;
	int ret = -EINVAL;

	switch (action->type) {
	case ACTION_SEND:
		fp_dbg("Sending %s", action->name);
		transfer = libusb_alloc_transfer(0);
		if (transfer == NULL) {
			fp_err("Failed to allocate transfer");
			fpi_imgdev_session_error(data->device, -ENOMEM);
			fpi_ssm_mark_aborted(ssm, -ENOMEM);
			return;
		}
		libusb_fill_bulk_transfer(transfer, data->device->udev,
					  action->endpoint, action->data,
					  action->size, async_send_cb, ssm,
					  data->timeout);
		ret = libusb_submit_transfer(transfer);
		break;

	case ACTION_RECEIVE:
		fp_dbg("Receiving %d bytes", action->size);
		transfer = libusb_alloc_transfer(0);
		if (transfer == NULL) {
			fp_err("Failed to allocate transfer");
			fpi_imgdev_session_error(data->device, -ENOMEM);
			fpi_ssm_mark_aborted(ssm, -ENOMEM);
			return;
		}
		libusb_fill_bulk_transfer(transfer, data->device->udev,
					  action->endpoint, data->receive_buf,
					  action->size, async_recv_cb, ssm,
					  data->timeout);
		ret = libusb_submit_transfer(transfer);
		break;

	default:
		fp_err("Bug detected: invalid action %d", action->type);
		fpi_imgdev_session_error(data->device, -EINVAL);
		fpi_ssm_mark_aborted(ssm, -EINVAL);
		return;
	}

	if (ret != 0) {
		fp_err("USB transfer error: %s", strerror(ret));
		fpi_imgdev_session_error(data->device, ret);
		fpi_ssm_mark_aborted(ssm, ret);
	}
}

static void usb_exchange_async(struct fpi_ssm *ssm,
			       struct usbexchange_data *data)
{
	struct fpi_ssm *subsm = fpi_ssm_new(data->device->dev,
					    usbexchange_loop,
					    data->stepcount);
	subsm->priv = data;
	fpi_ssm_start_subsm(ssm, subsm);
}

/* ====================== utils ======================= */

#if VFS5011_LINE_SIZE > INT_MAX/(256*256)
#error We might get integer overflow while computing standard deviation!
#endif

/* Calculade squared standand deviation */
static int get_deviation(unsigned char *buf, int size)
{
	int res = 0, mean = 0, i;
	for (i = 0; i < size; i++)
		mean += buf[i];

	mean /= size;

	for (i = 0; i < size; i++) {
		int dev = (int)buf[i] - mean;
		res += dev*dev;
	}

	return res / size;
}

/* Calculate mean square difference of two lines */
static int get_diff_norm(unsigned char *buf1, unsigned char *buf2, int size)
{
	int res = 0, i;
	for (i = 0; i < size; i++) {
		int dev = (int)buf1[i] - (int)buf2[i];
		res += dev*dev;
	}

	return res / size;
}

/* Calculade squared standand deviation of sum of two lines */
static int get_deviation2(unsigned char *buf1, unsigned char *buf2, int size)
{
	int res = 0, mean = 0, i;
	for (i = 0; i < size; i++)
		mean += (int)buf1[i] + (int)buf2[i];

	mean /= size;

	for (i = 0; i < size; i++) {
		int dev = (int)buf1[i] + (int)buf2[i] - mean;
		res += dev*dev;
	}

	return res / size;
}

static int cmpint(const void *p1, const void *p2, gpointer data)
{
	int a = *((int *)p1);
	int b = *((int *)p2);
	if (a < b)
		return -1;
	else if (a == b)
		return 0;
	else
		return 1;
}

static void median_filter(int *data, int size, int filtersize)
{
	int i;
	int *result = (int *)g_malloc0(size*sizeof(int));
	int *sortbuf = (int *)g_malloc0(filtersize*sizeof(int));
	for (i = 0; i < size; i++) {
		int i1 = i - (filtersize-1)/2;
		int i2 = i + (filtersize-1)/2;
		if (i1 < 0)
			i1 = 0;
		if (i2 >= size)
			i2 = size-1;
		g_memmove(sortbuf, data+i1, (i2-i1+1)*sizeof(int));
		g_qsort_with_data(sortbuf, i2-i1+1, sizeof(int), cmpint, NULL);
		result[i] = sortbuf[(i2-i1+1)/2];
	}
	memmove(data, result, size*sizeof(int));
	g_free(result);
	g_free(sortbuf);
}

void interpolate_lines(unsigned char *line1, float y1, unsigned char *line2,
		       float y2, unsigned char *output, float yi, int size)
{
	int i;
	for (i = 0; i < size; i++)
		output[i] = (float)line1[i]
			    + (yi-y1)/(y2-y1)*(line2[i]-line1[i]);
}

int min(int a, int b) {return (a < b) ? a : b; }

/* Rescale image to account for variable swiping speed */
int vfs5011_rescale_image(unsigned char *image, int input_lines,
			  unsigned char *output, int max_output_lines)
{
	/* Number of output lines per distance between two scanners */
	enum {
		RESOLUTION = 10,
		MEDIAN_FILTER_SIZE = 13,
		MAX_OFFSET = 10,
		GOOD_OFFSETS_CRITERION = 20,
		GOOD_OFFSETS_THRESHOLD = 3
	};
	int i;
	float y = 0.0;
	int line_ind = 0;
	int *offsets = (int *)g_malloc0(input_lines * sizeof(int));
#ifdef ENABLE_DEBUG_LOGGING
	gint64 start_time = g_get_real_time();
#endif

	for (i = 0; i < input_lines-1; i += 2) {
		int bestmatch = i;
		int bestdiff = 0;
		int j;

		int firstrow, lastrow;
		firstrow = i+1;
		lastrow = min(i + MAX_OFFSET, input_lines-1);

		for (j = firstrow; j <= lastrow; j++) {
			int diff = get_deviation2(
					image + i*VFS5011_LINE_SIZE + 56,
					image + j*VFS5011_LINE_SIZE + 168,
					64);
			if ((j == firstrow) || (diff < bestdiff)) {
				bestdiff = diff;
				bestmatch = j;
			}
		}
		offsets[i/2] = bestmatch - i;
		fp_dbg("offsets: %llu - %d", start_time, offsets[i/2]);
	}

	median_filter(offsets, input_lines-1, MEDIAN_FILTER_SIZE);

	fp_dbg("offsets_filtered: %llu", g_get_real_time());
	for (i = 0; i <= input_lines/2-1; i++)
		fp_dbg("%d", offsets[i]);
	for (i = 0; i < input_lines-1; i++) {
		int offset = offsets[i/2];
		if (offset > 0) {
			float ynext = y + (float)RESOLUTION / offset;
			while (line_ind < ynext) {
				if (line_ind > max_output_lines-1) {
					g_free(offsets);
					return line_ind;
				}
				interpolate_lines(
					image + i*VFS5011_LINE_SIZE + 8, y,
					image + (i+1)*VFS5011_LINE_SIZE + 8,
					ynext,
					output + line_ind*VFS5011_IMAGE_WIDTH,
					line_ind,
					VFS5011_IMAGE_WIDTH);
				line_ind++;
			}
			y = ynext;
		}
	}
	g_free(offsets);
	return line_ind;
}

/* ====================== main stuff ======================= */

enum {
	CAPTURE_LINES = 256,
	MAXLINES = 2000,
	MAX_CAPTURE_LINES = 100000,
};

struct vfs5011_data {
	unsigned char *total_buffer;
	unsigned char *capture_buffer;
	unsigned char *image_buffer;
	unsigned char *lastline;
	unsigned char *rescale_buffer;
	int lines_captured, lines_recorded, empty_lines;
	int max_lines_captured, max_lines_recorded;
	int lines_total, lines_total_allocated;
	gboolean loop_running;
	gboolean deactivating;
	struct usbexchange_data init_sequence;
};

enum {
	DEV_ACTIVATE_REQUEST_FPRINT,
	DEV_ACTIVATE_INIT_COMPLETE,
	DEV_ACTIVATE_READ_DATA,
	DEV_ACTIVATE_DATA_COMPLETE,
	DEV_ACTIVATE_PREPARE_NEXT_CAPTURE,
	DEV_ACTIVATE_NUM_STATES
};

enum {
	DEV_OPEN_START,
	DEV_OPEN_NUM_STATES
};

static void capture_init(struct vfs5011_data *data, int max_captured,
		int max_recorded)
{
	fp_dbg("capture_init");
	data->lastline = NULL;
	data->lines_captured = 0;
	data->lines_recorded = 0;
	data->empty_lines = 0;
	data->lines_total = 0;
	data->lines_total_allocated = 0;
	data->total_buffer = NULL;
	data->max_lines_captured = max_captured;
	data->max_lines_recorded = max_recorded;
}

static int process_chunk(struct vfs5011_data *data, int transferred)
{
	enum {
		DEVIATION_THRESHOLD = 15*15,
		DIFFERENCE_THRESHOLD = 600,
		STOP_CHECK_LINES = 50
	};

	fp_dbg("process_chunk: got %d bytes", transferred);
	int lines_captured = transferred/VFS5011_LINE_SIZE;
	int i;

	for (i = 0; i < lines_captured; i++) {
		unsigned char *linebuf = data->capture_buffer
					 + i * VFS5011_LINE_SIZE;

		if (get_deviation(linebuf + 8, VFS5011_IMAGE_WIDTH)
				< DEVIATION_THRESHOLD) {
			if (data->lines_captured == 0)
				continue;
			else
				data->empty_lines++;
		} else
			data->empty_lines = 0;
		if (data->empty_lines >= STOP_CHECK_LINES) {
			fp_dbg("process_chunk: got %d empty lines, finishing",
					data->empty_lines);
			return 1;
		}

		data->lines_captured++;
		if (data->lines_captured > data->max_lines_captured) {
			fp_dbg("process_chunk: captured %d lines, finishing",
					data->lines_captured);
			return 1;
		}

		if ((data->lastline == NULL)
			|| (get_diff_norm(
				data->lastline + 8,
				linebuf + 8,
				VFS5011_IMAGE_WIDTH) >= DIFFERENCE_THRESHOLD)) {
			data->lastline = data->image_buffer
					 + data->lines_recorded
					 * VFS5011_LINE_SIZE;
			memmove(data->lastline, linebuf, VFS5011_LINE_SIZE);
			data->lines_recorded++;
			if (data->lines_recorded >= data->max_lines_recorded) {
				fp_dbg("process_chunk: recorded %d lines, finishing",
						data->lines_recorded);
				return 1;
			}
		}
	}
	return 0;
}

void submit_image(struct fpi_ssm *ssm, struct vfs5011_data *data)
{
	struct fp_img_dev *dev = (struct fp_img_dev *)ssm->priv;
	int height = vfs5011_rescale_image(data->image_buffer,
					   data->lines_recorded,
					   data->rescale_buffer, MAXLINES);
	struct fp_img *img = fpi_img_new(VFS5011_IMAGE_WIDTH * height);

	if (img == NULL) {
		fp_err("Failed to create image");
		fpi_ssm_mark_aborted(ssm, -1);
	}

	img->flags = FP_IMG_V_FLIPPED;
	img->width = VFS5011_IMAGE_WIDTH;
	img->height = height;
	memmove(img->data, data->rescale_buffer, VFS5011_IMAGE_WIDTH * height);

	fp_dbg("Image captured, commiting");

	fpi_imgdev_image_captured(dev, img);
}

static void chunk_capture_callback(struct libusb_transfer *transfer)
{
	struct fpi_ssm *ssm = (struct fpi_ssm *)transfer->user_data;
	struct fp_img_dev *dev = (struct fp_img_dev *)ssm->priv;
	struct vfs5011_data *data = (struct vfs5011_data *)dev->priv;

	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED) ||
	    (transfer->status == LIBUSB_TRANSFER_TIMED_OUT)) {

		if (transfer->actual_length > 0)
			fpi_imgdev_report_finger_status(dev, TRUE);

		if (process_chunk(data, transfer->actual_length))
			fpi_ssm_jump_to_state(ssm, DEV_ACTIVATE_DATA_COMPLETE);
		else
			fpi_ssm_jump_to_state(ssm, DEV_ACTIVATE_READ_DATA);
	} else {
		fp_err("Failed to capture data");
		fpi_ssm_mark_aborted(ssm, -1);
	}
	libusb_free_transfer(transfer);
}

static int capture_chunk_async(struct vfs5011_data *data,
			       libusb_device_handle *handle, int nline,
			       int timeout, struct fpi_ssm *ssm)
{
	fp_dbg("capture_chunk_async: capture %d lines, already have %d",
		nline, data->lines_recorded);
	enum {
		DEVIATION_THRESHOLD = 15*15,
		DIFFERENCE_THRESHOLD = 600,
		STOP_CHECK_LINES = 50
	};

	struct libusb_transfer *transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(transfer, handle, VFS5011_IN_ENDPOINT_DATA,
				  data->capture_buffer,
				  nline * VFS5011_LINE_SIZE,
				  chunk_capture_callback, ssm, timeout);
	return libusb_submit_transfer(transfer);
}

static void async_sleep_cb(void *data)
{
	struct fpi_ssm *ssm = data;

	fpi_ssm_next_state(ssm);
}

/*
 *  Device initialization. Windows driver only does it when the device is
 *  plugged in, but it doesn't harm to do this every time before scanning the
 *  image.
 */
struct usb_action vfs5011_initialization[] = {
	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_01)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_19)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64) /* B5C457F9 */

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_00)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64) /* 0000FFFFFFFF */

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_01)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64) /* 0000FFFFFFFFFF */

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_02)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_01)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_1A)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_03)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_04)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 256)
	RECV(VFS5011_IN_ENDPOINT_DATA, 64)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_1A)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_05)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_01)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_06)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 17216)
	RECV(VFS5011_IN_ENDPOINT_DATA, 32)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_07)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 45056)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_08)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 16896)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_09)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 4928)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_10)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 5632)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_11)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 5632)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_12)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 3328)
	RECV(VFS5011_IN_ENDPOINT_DATA, 64)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_13)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_1A)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_03)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_14)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	RECV(VFS5011_IN_ENDPOINT_DATA, 4800)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_1A)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_02)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_27)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_1A)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_15)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_16)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 2368)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64)
	RECV(VFS5011_IN_ENDPOINT_DATA, 4800)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_17)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_init_18)
	/* 0000 */
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	/*
	 * Windows driver does this and it works
	 * But in this driver this call never returns...
	 * RECV(VFS5011_IN_ENDPOINT_CTRL2, 8) //00D3054000
	 */
};

/* Initiate recording the image */
struct usb_action vfs5011_initiate_capture[] = {
	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_04)
	RECV(VFS5011_IN_ENDPOINT_DATA, 64)
	RECV(VFS5011_IN_ENDPOINT_DATA, 84032)
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_1A)
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_prepare_00)
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_cmd_1A)
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_prepare_01)
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_prepare_02)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 2368)
	RECV(VFS5011_IN_ENDPOINT_CTRL, 64)
	RECV(VFS5011_IN_ENDPOINT_DATA, 4800)

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_prepare_03)
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 64, VFS5011_NORMAL_CONTROL_REPLY)
	/*
	 * Windows driver does this and it works
	 * But in this driver this call never returns...
	 * RECV(VFS5011_IN_ENDPOINT_CTRL2, 8);
	 */

	SEND(VFS5011_OUT_ENDPOINT, vfs5011_prepare_04)
	RECV_CHECK(VFS5011_IN_ENDPOINT_CTRL, 2368, VFS5011_NORMAL_CONTROL_REPLY)

	/*
	 * Windows driver does this and it works
	 * But in this driver this call never returns...
	 * RECV(VFS5011_IN_ENDPOINT_CTRL2, 8);
	 */
};

/* ====================== lifprint interface ======================= */

static void activate_loop(struct fpi_ssm *ssm)
{
	enum {READ_TIMEOUT = 0};

	struct fp_img_dev *dev = (struct fp_img_dev *)ssm->priv;
	struct vfs5011_data *data = (struct vfs5011_data *)dev->priv;
	int r;
	struct fpi_timeout *timeout;

	fp_dbg("main_loop: state %d", ssm->cur_state);

	switch (ssm->cur_state) {
	case DEV_ACTIVATE_REQUEST_FPRINT:
		data->init_sequence.stepcount =
			array_n_elements(vfs5011_initiate_capture);
		data->init_sequence.actions = vfs5011_initiate_capture;
		data->init_sequence.device = dev;
		if (data->init_sequence.receive_buf == NULL)
			data->init_sequence.receive_buf =
				g_malloc0(VFS5011_RECEIVE_BUF_SIZE);
		data->init_sequence.timeout = 1000;
		usb_exchange_async(ssm, &data->init_sequence);
		break;

	case DEV_ACTIVATE_INIT_COMPLETE:
		if (data->init_sequence.receive_buf != NULL)
			g_free(data->init_sequence.receive_buf);
		data->init_sequence.receive_buf = NULL;
		capture_init(data, MAX_CAPTURE_LINES, MAXLINES);
		fpi_imgdev_activate_complete(dev, 0);
		fpi_ssm_next_state(ssm);
		break;

	case DEV_ACTIVATE_READ_DATA:
		if (data->deactivating) {
			fp_dbg("deactivating, marking completed");
			fpi_ssm_mark_completed(ssm);
		} else {
			r = capture_chunk_async(data, dev->udev, CAPTURE_LINES,
						READ_TIMEOUT, ssm);
			if (r != 0) {
				fp_err("Failed to capture data");
				fpi_imgdev_session_error(dev, r);
				fpi_ssm_mark_aborted(ssm, r);
			}
		}
		break;

	case DEV_ACTIVATE_DATA_COMPLETE:
		timeout = fpi_timeout_add(1, async_sleep_cb, ssm);

		if (timeout == NULL) {
			/* Failed to add timeout */
			fp_err("failed to add timeout");
			fpi_imgdev_session_error(dev, -1);
			fpi_ssm_mark_aborted(ssm, -1);
		}
		break;

	case DEV_ACTIVATE_PREPARE_NEXT_CAPTURE:
		data->init_sequence.stepcount =
			array_n_elements(vfs5011_initiate_capture);
		data->init_sequence.actions = vfs5011_initiate_capture;
		data->init_sequence.device = dev;
		if (data->init_sequence.receive_buf == NULL)
			data->init_sequence.receive_buf =
				g_malloc0(VFS5011_RECEIVE_BUF_SIZE);
		data->init_sequence.timeout = VFS5011_DEFAULT_WAIT_TIMEOUT;
		usb_exchange_async(ssm, &data->init_sequence);
		break;

	}
}

static void activate_loop_complete(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = (struct fp_img_dev *)ssm->priv;
	struct vfs5011_data *data = (struct vfs5011_data *)dev->priv;
	int r = ssm->error;

	fp_dbg("finishing");
	if (data->init_sequence.receive_buf != NULL)
		g_free(data->init_sequence.receive_buf);
	data->init_sequence.receive_buf = NULL;
	data->loop_running = FALSE;
	submit_image(ssm, data);
	fpi_imgdev_report_finger_status(dev, FALSE);

	fpi_ssm_free(ssm);

	if (r)
		fpi_imgdev_session_error(dev, r);

	if (data->deactivating)
		fpi_imgdev_deactivate_complete(dev);
}

static void open_loop(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = (struct fp_img_dev *)ssm->priv;
	struct vfs5011_data *data = (struct vfs5011_data *)dev->priv;

	switch (ssm->cur_state) {
	case DEV_OPEN_START:
		data->init_sequence.stepcount =
			array_n_elements(vfs5011_initialization);
		data->init_sequence.actions = vfs5011_initialization;
		data->init_sequence.device = dev;
		data->init_sequence.receive_buf =
			g_malloc0(VFS5011_RECEIVE_BUF_SIZE);
		data->init_sequence.timeout = VFS5011_DEFAULT_WAIT_TIMEOUT;
		usb_exchange_async(ssm, &data->init_sequence);
		break;
	};
}

static void open_loop_complete(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = (struct fp_img_dev *)ssm->priv;
	struct vfs5011_data *data = (struct vfs5011_data *)dev->priv;

	g_free(data->init_sequence.receive_buf);
	data->init_sequence.receive_buf = NULL;

	fpi_imgdev_open_complete(dev, 0);
	fpi_ssm_free(ssm);
}

static int dev_open(struct fp_img_dev *dev, unsigned long driver_data)
{

	struct vfs5011_data *data;
	int r;

	data = (struct vfs5011_data *)g_malloc0(sizeof(*data));
	data->capture_buffer =
		(unsigned char *)g_malloc0(CAPTURE_LINES * VFS5011_LINE_SIZE);
	data->image_buffer =
		(unsigned char *)g_malloc0(MAXLINES * VFS5011_LINE_SIZE);
	data->rescale_buffer =
		(unsigned char *)g_malloc0(MAXLINES * VFS5011_IMAGE_WIDTH);
	dev->priv = data;

	dev->dev->nr_enroll_stages = 1;

	r = libusb_reset_device(dev->udev);
	if (r != 0) {
		fp_err("Failed to reset the device");
		return r;
	}

	r = libusb_claim_interface(dev->udev, 0);
	if (r != 0) {
		fp_err("Failed to claim interface");
		return r;
	}

	struct fpi_ssm *ssm;
	ssm = fpi_ssm_new(dev->dev, open_loop, DEV_OPEN_NUM_STATES);
	ssm->priv = dev;
	fpi_ssm_start(ssm, open_loop_complete);

	return 0;
}

static void dev_close(struct fp_img_dev *dev)
{
	libusb_release_interface(dev->udev, 0);
	struct vfs5011_data *data = (struct vfs5011_data *)dev->priv;
	if (data != NULL) {
		g_free(data->capture_buffer);
		g_free(data->image_buffer);
		g_free(data->rescale_buffer);
		g_free(data);
	}
	fpi_imgdev_close_complete(dev);
}

static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	struct vfs5011_data *data = (struct vfs5011_data *)dev->priv;
	struct fpi_ssm *ssm;

	fp_dbg("device initialized");
	data->deactivating = FALSE;
	data->loop_running = TRUE;

	fp_dbg("creating ssm");
	ssm = fpi_ssm_new(dev->dev, activate_loop, DEV_ACTIVATE_NUM_STATES);
	ssm->priv = dev;
	fp_dbg("starting ssm");
	fpi_ssm_start(ssm, activate_loop_complete);
	fp_dbg("ssm done, getting out");

	return 0;
}

static void dev_deactivate(struct fp_img_dev *dev)
{
	struct vfs5011_data *data = dev->priv;
	if (data->loop_running)
		data->deactivating = TRUE;
	else
		fpi_imgdev_deactivate_complete(dev);
}

static const struct usb_id id_table[] = {
	{ .vendor = 0x138a, .product = 0x0011 /* vfs5011 */ },
	{ .vendor = 0x138a, .product = 0x0017 /* Validity device from Lenovo T440 laptops */ },
	{ .vendor = 0x138a, .product = 0x0018 /* one more Validity device */ },
	{ 0, 0, 0, },
};

struct fp_img_driver vfs5011_driver = {
	.driver = {
		.id = VFS5011_ID,
		.name = "vfs5011",
		.full_name = "Validity VFS5011",
		.id_table = id_table,
		.scan_type = FP_SCAN_TYPE_SWIPE,
	},

	.flags = 0,
	.img_width = VFS5011_IMAGE_WIDTH,
	.img_height = -1,
	.bz3_threshold = 20,

	.open = dev_open,
	.close = dev_close,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
};

