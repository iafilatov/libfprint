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

void aes_assemble_image(unsigned char *input, size_t width, size_t height,
	unsigned char *output)
{
	size_t row, column;

	for (column = 0; column < width; column++) {
		for (row = 0; row < height; row += 2) {
			output[width * row + column] = (*input & 0x0f) * 17;
			output[width * (row + 1) + column] = ((*input & 0xf0) >> 4) * 17;
			input++;
		}
	}
}

/* find overlapping parts of  frames */
static unsigned int find_overlap(unsigned char *first_frame,
	unsigned char *second_frame, unsigned int *min_error,
	unsigned int frame_width, unsigned int frame_height)
{
	unsigned int dy;
	unsigned int not_overlapped_height = 0;
	/* 255 is highest brightness value for an 8bpp image */
	*min_error = 255 * frame_width * frame_height;
	for (dy = 0; dy < frame_height; dy++) {
		/* Calculating difference (error) between parts of frames */
		unsigned int i;
		unsigned int error = 0;
		for (i = 0; i < frame_width * (frame_height - dy); i++) {
			/* Using ? operator to avoid abs function */
			error += first_frame[i] > second_frame[i] ?
					(first_frame[i] - second_frame[i]) :
					(second_frame[i] - first_frame[i]);
		}

		/* Normalize error */
		error *= 15;
		error /= i;
		if (error < *min_error) {
			*min_error = error;
			not_overlapped_height = dy;
		}
		first_frame += frame_width;
	}

	return not_overlapped_height;
}

/* assemble a series of frames into a single image */
static unsigned int assemble(GSList *list_entry, size_t num_stripes,
	unsigned int frame_width, unsigned int frame_height,
	unsigned char *output, gboolean reverse, unsigned int *errors_sum)
{
	uint8_t *assembled = output;
	int frame;
	uint32_t image_height = frame_height;
	unsigned int min_error, frame_size = frame_width * frame_height;
	*errors_sum = 0;

	if (reverse)
		output += (num_stripes - 1) * frame_size;
	for (frame = 0; frame < num_stripes; frame++) {
		aes_assemble_image(list_entry->data, frame_width, frame_height, output);

		if (reverse)
		    output -= frame_size;
		else
		    output += frame_size;
		list_entry = g_slist_next(list_entry);
	}

	/* Detecting where frames overlaped */
	output = assembled;
	for (frame = 1; frame < num_stripes; frame++) {
		int not_overlapped;

		output += frame_size;
		not_overlapped = find_overlap(assembled, output, &min_error,
			frame_width, frame_height);
		*errors_sum += min_error;
		image_height += not_overlapped;
		assembled += frame_width * not_overlapped;
		memcpy(assembled, output, frame_size);
	}
	return image_height;
}

struct fp_img *aes_assemble(GSList *stripes, size_t stripes_len,
	unsigned int frame_width, unsigned int frame_height)
{
	size_t final_size;
	struct fp_img *img;
	unsigned int frame_size = frame_width * frame_height;
	unsigned int errors_sum, r_errors_sum;

	BUG_ON(stripes_len == 0);

	/* create buffer big enough for max image */
	img = fpi_img_new(stripes_len * frame_size);

	img->flags = FP_IMG_COLORS_INVERTED;
	img->height = assemble(stripes, stripes_len,
		frame_width, frame_height,
		img->data, FALSE, &errors_sum);
	img->height = assemble(stripes, stripes_len,
		frame_width, frame_height,
		img->data, TRUE, &r_errors_sum);

	if (r_errors_sum > errors_sum) {
		img->height = assemble(stripes, stripes_len,
			frame_width, frame_height,
			img->data, FALSE, &errors_sum);
		img->flags |= FP_IMG_V_FLIPPED | FP_IMG_H_FLIPPED;
		fp_dbg("normal scan direction");
	} else {
		fp_dbg("reversed scan direction");
	}

	/* now that overlap has been removed, resize output image buffer */
	final_size = img->height * frame_width;
	img = fpi_img_resize(img, final_size);
	img->width = frame_width;

	return img;
}
