/*
 * Image management functions for libfprint
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

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "fp_internal.h"

struct fp_img *fpi_img_new(size_t length)
{
	struct fp_img *img = g_malloc(sizeof(*img) + length);
	memset(img, 0, sizeof(*img));
	fp_dbg("length=%zd", length);
	img->length = length;
	return img;
}

struct fp_img *fpi_img_new_dims(int width, int height)
{
	struct fp_img *img = fpi_img_new(width * height);
	img->width = width;
	img->height = height;
	return img;
}

gboolean fpi_img_is_sane(struct fp_img *img)
{
	/* basic checks */
	if (!img->length || !img->width || !img->height)
		return FALSE;

	/* buffer is big enough? */
	if ((img->length * img->height) < img->length)
		return FALSE;

	return TRUE;
}

struct fp_img *fpi_img_resize(struct fp_img *img, size_t newsize)
{
	return g_realloc(img, sizeof(*img) + newsize);
}

API_EXPORTED int fp_img_get_height(struct fp_img *img)
{
	return img->height;
}

API_EXPORTED int fp_img_get_width(struct fp_img *img)
{
	return img->width;
}

API_EXPORTED unsigned char *fp_img_get_data(struct fp_img *img)
{
	return img->data;
}

API_EXPORTED int fp_img_save_to_file(struct fp_img *img, char *path)
{
	FILE *fd = fopen(path, "w");
	size_t write_size = img->width * img->height;
	int r;

	if (!fd) {
		fp_dbg("could not open '%s' for writing: %d", path, errno);
		return -errno;
	}

	r = fprintf(fd, "P5 %d %d 255\n", img->width, img->height);
	if (r < 0) {
		fp_err("pgm header write failed, error %d", r);
		return r;
	}

	r = fwrite(img->data, 1, write_size, fd);
	if (r < write_size) {
		fp_err("short write (%d)", r);
		return -EIO;
	}

	fclose(fd);
	fp_dbg("written to '%s'", path);
	return 0;
}

static void vflip(struct fp_img *img)
{
	int width = img->width;
	int data_len = img->width * img->height;
	unsigned char rowbuf[width];
	int i;

	for (i = 0; i < img->height / 2; i++) {
		int offset = i * width;
		int swap_offset = data_len - (width * (i + 1));

		/* copy top row into buffer */
		memcpy(rowbuf, img->data + offset, width);

		/* copy lower row over upper row */
		memcpy(img->data + offset, img->data + swap_offset, width);

		/* copy buffer over lower row */
		memcpy(img->data + swap_offset, rowbuf, width);
	}
}

static void hflip(struct fp_img *img)
{
	int width = img->width;
	unsigned char rowbuf[width];
	int i, j;

	for (i = 0; i < img->height; i++) {
		int offset = i * width;

		memcpy(rowbuf, img->data + offset, width);
		for (j = 0; j < width; j++)
			img->data[offset + j] = rowbuf[width - j - 1];
	}
}

static void invert_colors(struct fp_img *img)
{
	int data_len = img->width * img->height;
	int i;
	for (i = 0; i < data_len; i++)
		img->data[i] = 0xff - img->data[i];
}

API_EXPORTED void fp_img_standardize(struct fp_img *img)
{
	if (img->flags & FP_IMG_V_FLIPPED) {
		vflip(img);
		img->flags &= ~FP_IMG_V_FLIPPED;
	}
	if (img->flags & FP_IMG_H_FLIPPED) {
		hflip(img);
		img->flags &= ~FP_IMG_H_FLIPPED;
	}
	if (img->flags & FP_IMG_COLORS_INVERTED) {
		invert_colors(img);
		img->flags &= ~FP_IMG_COLORS_INVERTED;
	}
}
