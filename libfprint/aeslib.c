/*
 * Shared functions between libfprint Authentec drivers
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
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

#define FP_COMPONENT "aeslib"

#include <errno.h>
#include <string.h>

#include <libusb.h>
#include <glib.h>

#include "fp_internal.h"
#include "aeslib.h"

#define MAX_REGWRITES_PER_REQUEST	16

#define BULK_TIMEOUT	4000
#define EP_IN			(1 | LIBUSB_ENDPOINT_IN)
#define EP_OUT			(2 | LIBUSB_ENDPOINT_OUT)

struct write_regv_data {
	struct fp_img_dev *imgdev;
	unsigned int num_regs;
	const struct aes_regwrite *regs;
	unsigned int offset;
	aes_write_regv_cb callback;
	void *user_data;
};

static void continue_write_regv(struct write_regv_data *wdata);

/* libusb bulk callback for regv write completion transfer. continues the
 * transaction */
static void write_regv_trf_complete(struct libusb_transfer *transfer)
{
	struct write_regv_data *wdata = transfer->user_data;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
		wdata->callback(wdata->imgdev, -EIO, wdata->user_data);
	else if (transfer->length != transfer->actual_length)
		wdata->callback(wdata->imgdev, -EPROTO, wdata->user_data);
	else
		continue_write_regv(wdata);

	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

/* write from wdata->offset to upper_bound (inclusive) of wdata->regs */
static int do_write_regv(struct write_regv_data *wdata, int upper_bound)
{
	unsigned int offset = wdata->offset;
	unsigned int num = upper_bound - offset + 1;
	size_t alloc_size = num * 2;
	unsigned char *data = g_malloc(alloc_size);
	unsigned int i;
	size_t data_offset = 0;
	struct libusb_transfer *transfer = libusb_alloc_transfer(0);
	int r;

	if (!transfer) {
		g_free(data);
		return -ENOMEM;
	}

	for (i = offset; i < offset + num; i++) {
		const struct aes_regwrite *regwrite = &wdata->regs[i];
		data[data_offset++] = regwrite->reg;
		data[data_offset++] = regwrite->value;
	}

	libusb_fill_bulk_transfer(transfer, wdata->imgdev->udev, EP_OUT, data,
		alloc_size, write_regv_trf_complete, wdata, BULK_TIMEOUT);
	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(data);
		libusb_free_transfer(transfer);
	}

	return r;
}

/* write the next batch of registers to be written, or if there are no more,
 * indicate completion to the caller */
static void continue_write_regv(struct write_regv_data *wdata)
{
	unsigned int offset = wdata->offset;
	unsigned int regs_remaining;
	unsigned int limit;
	unsigned int upper_bound;
	int i;
	int r;

	/* skip all zeros and ensure there is still work to do */
	while (TRUE) {
		if (offset >= wdata->num_regs) {
			fp_dbg("all registers written");
			wdata->callback(wdata->imgdev, 0, wdata->user_data);
			return;
		}
		if (wdata->regs[offset].reg)
			break;
		offset++;
	}

	wdata->offset = offset;
	regs_remaining = wdata->num_regs - offset;
	limit = MIN(regs_remaining, MAX_REGWRITES_PER_REQUEST);
	upper_bound = offset + limit - 1;

	/* determine if we can write the entire of the regs at once, or if there
	 * is a zero dividing things up */
	for (i = offset; i <= upper_bound; i++)
		if (!wdata->regs[i].reg) {
			upper_bound = i - 1;
			break;
		}

	r = do_write_regv(wdata, upper_bound);
	if (r < 0) {
		wdata->callback(wdata->imgdev, r, wdata->user_data);
		return;
	}

	wdata->offset = upper_bound + 1;
}

/* write a load of registers to the device, combining multiple writes in a
 * single URB up to a limit. insert writes to non-existent register 0 to force
 * specific groups of writes to be separated by different URBs. */
void aes_write_regv(struct fp_img_dev *dev, const struct aes_regwrite *regs,
	unsigned int num_regs, aes_write_regv_cb callback, void *user_data)
{
	struct write_regv_data *wdata = g_malloc(sizeof(*wdata));
	fp_dbg("write %d regs", num_regs);
	wdata->imgdev = dev;
	wdata->num_regs = num_regs;
	wdata->regs = regs;
	wdata->offset = 0;
	wdata->callback = callback;
	wdata->user_data = user_data;
	continue_write_regv(wdata);
}

static inline unsigned char aes_get_pixel(struct aes_stripe *frame,
					  unsigned int x,
					  unsigned int y,
					  unsigned int frame_width,
					  unsigned int frame_height)
{
	unsigned char ret;

	ret = frame->data[x * (frame_height >> 1) + (y >> 1)];
	ret = y % 2 ? ret >> 4 : ret & 0xf;
	ret *= 17;

	return ret;
}

static unsigned int calc_error(struct aes_stripe *first_frame,
			       struct aes_stripe *second_frame,
			       int dx,
			       int dy,
			       unsigned int frame_width,
			       unsigned int frame_height)
{
	unsigned int width, height;
	unsigned int x1, y1, x2, y2, err, i, j;

	width = frame_width - (dx > 0 ? dx : -dx);
	height = frame_height - dy;

	y1 = 0;
	y2 = dy;
	i = 0;
	err = 0;
	do {
		x1 = dx < 0 ? 0 : dx;
		x2 = dx < 0 ? -dx : 0;
		j = 0;

		do {
			unsigned char v1, v2;


			v1 = aes_get_pixel(first_frame, x1, y1, frame_width, frame_height);
			v2 = aes_get_pixel(second_frame, x2, y2, frame_width, frame_height);
			err += v1 > v2 ? v1 - v2 : v2 - v1;
			j++;
			x1++;
			x2++;

		} while (j < width);
		i++;
		y1++;
		y2++;
	} while (i < height);

	/* Normalize error */
	err *= (frame_height * frame_width);
	err /= (height * width);

	if (err == 0)
		return INT_MAX;

	return err;
}

/* This function is rather CPU-intensive. It's better to use hardware
 * to detect movement direction when possible.
 */
static void find_overlap(struct aes_stripe *first_frame,
			 struct aes_stripe *second_frame,
			 unsigned int *min_error,
			 unsigned int frame_width,
			 unsigned int frame_height)
{
	int dx, dy;
	unsigned int err;
	*min_error = INT_MAX;

	/* Seeking in horizontal and vertical dimensions,
	 * for horizontal dimension we'll check only 8 pixels
	 * in both directions. For vertical direction diff is
	 * rarely less than 2, so start with it.
	 */
	for (dy = 2; dy < frame_height; dy++) {
		for (dx = -8; dx < 8; dx++) {
			err = calc_error(first_frame, second_frame,
				dx, dy, frame_width, frame_height);
			if (err < *min_error) {
				*min_error = err;
				second_frame->delta_x = -dx;
				second_frame->delta_y = dy;
			}
		}
	}
}

unsigned int aes_calc_delta(GSList *stripes, size_t num_stripes,
	unsigned int frame_width, unsigned int frame_height,
	gboolean reverse)
{
	GSList *list_entry = stripes;
	GTimer *timer;
	int frame = 1;
	int height = 0;
	struct aes_stripe *prev_stripe = list_entry->data;
	unsigned int min_error;

	list_entry = g_slist_next(list_entry);

	timer = g_timer_new();
	do {
		struct aes_stripe *cur_stripe = list_entry->data;

		if (reverse) {
			find_overlap(prev_stripe, cur_stripe, &min_error,
				frame_width, frame_height);
			prev_stripe->delta_y = -prev_stripe->delta_y;
			prev_stripe->delta_x = -prev_stripe->delta_x;
		}
		else
			find_overlap(cur_stripe, prev_stripe, &min_error,
				frame_width, frame_height);

		frame++;
		height += prev_stripe->delta_y;
		prev_stripe = cur_stripe;
		list_entry = g_slist_next(list_entry);

	} while (frame < num_stripes);

	if (height < 0)
		height = -height;
	height += frame_height;
	g_timer_stop(timer);
	fp_dbg("calc delta completed in %f secs", g_timer_elapsed(timer, NULL));
	g_timer_destroy(timer);

	return height;
}

static inline void aes_blit_stripe(struct fp_img *img,
				   struct aes_stripe *stripe,
				   int x, int y, unsigned int frame_width,
				   unsigned int frame_height)
{
	unsigned int ix, iy;
	unsigned int fx, fy;
	unsigned int width, height;

	/* Find intersection */
	if (x < 0) {
		width = frame_width + x;
		ix = 0;
		fx = -x;
	} else {
		ix = x;
		fx = 0;
		width = frame_width;
	}
	if ((ix + width) > img->width)
		width = img->width - ix;

	if (y < 0) {
		iy = 0;
		fy = -y;
		height = frame_height + y;
	} else {
		iy = y;
		fy = 0;
		height = frame_height;
	}

	if (fx > frame_width)
		return;

	if (fy > frame_height)
		return;

	if (ix > img->width)
		return;

	if (iy > img->height)
		return;

	if ((iy + height) > img->height)
		height = img->height - iy;

	for (; fy < height; fy++, iy++) {
		if (x < 0) {
			ix = 0;
			fx = -x;
		} else {
			ix = x;
			fx = 0;
		}
		for (; fx < width; fx++, ix++) {
			img->data[ix + (iy * img->width)] = aes_get_pixel(stripe, fx, fy, frame_width, frame_height);
		}
	}
}

struct fp_img *aes_assemble(GSList *stripes, size_t stripes_len,
			    unsigned int frame_width, unsigned int frame_height, unsigned int img_width)
{
	GSList *stripe;
	struct fp_img *img;
	int height = 0;
	int i, y, x;
	gboolean reverse = FALSE;
	struct aes_stripe *aes_stripe;

	BUG_ON(stripes_len == 0);
	BUG_ON(img_width < frame_width);

	/* Calculate height */
	i = 0;
	stripe = stripes;

	/* No offset for 1st image */
	aes_stripe = stripe->data;
	aes_stripe->delta_x = 0;
	aes_stripe->delta_y = 0;
	do {
		aes_stripe = stripe->data;

		height += aes_stripe->delta_y;
		i++;
		stripe = g_slist_next(stripe);
	} while (i < stripes_len);

	fp_dbg("height is %d", height);

	if (height < 0) {
		reverse = TRUE;
		height = -height;
	}

	/* For last frame */
	height += frame_height;

	/* Create buffer big enough for max image */
	img = fpi_img_new(img_width * height);
	img->flags = FP_IMG_COLORS_INVERTED;
	img->width = img_width;
	img->height = height;

	/* Assemble stripes */
	i = 0;
	stripe = stripes;
	y = reverse ? (height - frame_height) : 0;
	x = (img_width - frame_width) / 2;

	do {
		aes_stripe = stripe->data;

		y += aes_stripe->delta_y;
		x += aes_stripe->delta_x;

		aes_blit_stripe(img, aes_stripe, x, y, frame_width, frame_height);

		stripe = g_slist_next(stripe);
		i++;
	} while (i < stripes_len);

	return img;
}
