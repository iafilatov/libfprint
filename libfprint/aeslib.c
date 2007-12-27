/*
 * Shared functions between libfprint Authentec drivers
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
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

#include <glib.h>

#include "fp_internal.h"
#include "aeslib.h"

#define MAX_REGWRITES_PER_REQUEST	16

#define BULK_TIMEOUT	4000
#define EP_IN			(1 | USB_ENDPOINT_IN)
#define EP_OUT			(2 | USB_ENDPOINT_OUT)

static int do_write_regv(struct fp_img_dev *dev,
	const struct aes_regwrite *regs, unsigned int num)
{
	size_t alloc_size = num * 2;
	unsigned char *data = g_malloc(alloc_size);
	unsigned int i;
	size_t offset = 0;
	int r;

	for (i = 0; i < num; i++) {
		data[offset++] = regs[i].reg;
		data[offset++] = regs[i].value;
	}

	r = usb_bulk_write(dev->udev, EP_OUT, data, alloc_size, BULK_TIMEOUT);
	g_free(data);
	if (r < 0) {
		fp_err("bulk write error %d", r);
		return r;
	} else if ((unsigned int) r < alloc_size) {
		fp_err("unexpected short write %d/%d", r, alloc_size);
		return -EIO;
	}

	return 0;
}

int aes_write_regv(struct fp_img_dev *dev, const struct aes_regwrite *regs,
	unsigned int num)
{
	unsigned int i;
	int skip = 0;
	int add_offset = 0;
	fp_dbg("write %d regs", num);

	for (i = 0; i < num; i += add_offset + skip) {
		int r, j;
		int limit = MIN(num, i + MAX_REGWRITES_PER_REQUEST);
		skip = 0;

		if (!regs[i].reg) {
			add_offset = 0;
			skip = 1;
			continue;
		}

		for (j = i; j < limit; j++)
			if (!regs[j].reg) {
				skip = 1;
				break;
			}

		add_offset = j - i;
		r = do_write_regv(dev, &regs[i], add_offset);
		if (r < 0)
			return r;
	}

	return 0;
}

void aes_assemble_image(unsigned char *input, size_t width, size_t height,
	unsigned char *output)
{
	size_t row, column;

	for (column = 0; column < width; column++) {
		for (row = 0; row < height; row += 2) {
			output[width * row + column] = (*input & 0x07) * 36;
			output[width * (row + 1) + column] = ((*input & 0x70) >> 4) * 36;
			input++;
		}
	}
}

