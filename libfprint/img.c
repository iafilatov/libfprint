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
#include "nbis/include/bozorth.h"
#include "nbis/include/lfs.h"

struct fp_img *fpi_img_new(size_t length)
{
	struct fp_img *img = g_malloc(sizeof(*img) + length);
	memset(img, 0, sizeof(*img));
	fp_dbg("length=%zd", length);
	img->length = length;
	return img;
}

struct fp_img *fpi_img_new_for_imgdev(struct fp_img_dev *imgdev)
{
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(imgdev->dev->drv);
	int width = imgdrv->img_width;
	int height = imgdrv->img_height;
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

API_EXPORTED void fp_img_free(struct fp_img *img)
{
	g_free(img);
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

/* Based on write_minutiae_XYTQ and bz_load */
static void minutiae_to_xyt(MINUTIAE *minutiae, int bwidth,
	int bheight, unsigned char *buf)
{
	int i;
	MINUTIA *minutia;
	struct minutiae_struct c[MAX_FILE_MINUTIAE];
	struct xyt_struct *xyt = (struct xyt_struct *) buf;

	/* FIXME: only considers first 150 minutiae (MAX_FILE_MINUTIAE) */
	/* nist does weird stuff with 150 vs 1000 limits */
	int nmin = min(minutiae->num, MAX_FILE_MINUTIAE);

	for (i = 0; i < nmin; i++){
		minutia = minutiae->list[i];

		lfs2nist_minutia_XYT(&c[i].col[0], &c[i].col[1], &c[i].col[2],
				minutia, bwidth, bheight);
		c[i].col[3] = sround(minutia->reliability * 100.0);

		if (c[i].col[2] > 180)
			c[i].col[2] -= 360;
	}

	qsort((void *) &c, (size_t) nmin, sizeof(struct minutiae_struct),
			sort_x_y);

	for (i = 0; i < nmin; i++) {
		xyt->xcol[i]     = c[i].col[0];
		xyt->ycol[i]     = c[i].col[1];
		xyt->thetacol[i] = c[i].col[2];
	}
	xyt->nrows = nmin;
}

int fpi_img_detect_minutiae(struct fp_img_dev *imgdev, struct fp_img *img,
	struct fp_print_data **ret)
{
	MINUTIAE *minutiae;
	int r;
	int *direction_map, *low_contrast_map, *low_flow_map;
	int *high_curve_map, *quality_map;
	int map_w, map_h;
	unsigned char *bdata;
	int bw, bh, bd;
	struct fp_print_data *print;
	GTimer *timer;

	/* 25.4 mm per inch */
	timer = g_timer_new();
	r = get_minutiae(&minutiae, &quality_map, &direction_map,
                         &low_contrast_map, &low_flow_map, &high_curve_map,
                         &map_w, &map_h, &bdata, &bw, &bh, &bd,
                         img->data, img->width, img->height, 8,
						 DEFAULT_PPI / (double)25.4, &lfsparms_V2);
	g_timer_stop(timer);
	fp_dbg("minutiae scan completed in %f secs", g_timer_elapsed(timer, NULL));
	g_timer_destroy(timer);
	if (r) {
		fp_err("get minutiae failed, code %d", r);
		return r;
	}
	fp_dbg("detected %d minutiae", minutiae->num);
	r = minutiae->num;

	/* FIXME: space is wasted if we dont hit the max minutiae count. would
	 * be good to make this dynamic. */
	print = fpi_print_data_new(imgdev->dev, sizeof(struct xyt_struct));
	print->type = PRINT_DATA_NBIS_MINUTIAE;
	minutiae_to_xyt(minutiae, bw, bh, print->data);
	/* FIXME: the print buffer at this point is endian-specific, and will
	 * only work when loaded onto machines with identical endianness. not good!
	 * data format should be platform-independant. */
	*ret = print;

	free_minutiae(minutiae);
	free(quality_map);
	free(direction_map);
	free(low_contrast_map);
	free(low_flow_map);
	free(high_curve_map);
	free(bdata);

	return r;
}

int fpi_img_compare_print_data(struct fp_print_data *enrolled_print,
	struct fp_print_data *new_print)
{
	struct xyt_struct *gstruct = (struct xyt_struct *) enrolled_print->data;
	struct xyt_struct *pstruct = (struct xyt_struct *) new_print->data;
	GTimer *timer;
	int r;

	if (enrolled_print->type != PRINT_DATA_NBIS_MINUTIAE ||
			new_print->type != PRINT_DATA_NBIS_MINUTIAE) {
		fp_err("invalid print format");
		return -EINVAL;
	}

	timer = g_timer_new();
	r = bozorth_main(pstruct, gstruct);
	g_timer_stop(timer);
	fp_dbg("bozorth processing took %f seconds, score=%d",
		g_timer_elapsed(timer, NULL), r);
	g_timer_destroy(timer);

	return r;
}
