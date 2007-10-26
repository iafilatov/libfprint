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

