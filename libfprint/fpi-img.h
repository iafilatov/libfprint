/*
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2018 Bastien Nocera <hadess@hadess.net>
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

#ifndef __FPI_IMG_H__
#define __FPI_IMG_H__

#include <stdint.h>

struct fp_minutiae;

/**
 * FpiImgFlags:
 * @FP_IMG_V_FLIPPED: the image is vertically flipped
 * @FP_IMG_H_FLIPPED: the image is horizontally flipped
 * @FP_IMG_COLORS_INVERTED: the colours are inverted
 * @FP_IMG_BINARIZED_FORM: binarised image, see fp_img_binarize()
 * @FP_IMG_PARTIAL: the image is partial, useful for driver to keep track
 *   of incomplete captures
 *
 * Flags used in the #fp_img structure to describe the image contained
 * into the structure. Note that a number of functions will refuse to
 * handle images which haven't been standardised through fp_img_standardize()
 * (meaning the @FP_IMG_V_FLIPPED, @FP_IMG_H_FLIPPED and @FP_IMG_COLORS_INVERTED
 * should all be unset when the image needs to be analysed).
 */
typedef enum {
	FP_IMG_V_FLIPPED       = 1 << 0,
	FP_IMG_H_FLIPPED       = 1 << 1,
	FP_IMG_COLORS_INVERTED = 1 << 2,
	FP_IMG_BINARIZED_FORM  = 1 << 3,
	FP_IMG_PARTIAL         = 1 << 4
} FpiImgFlags;

/**
 * fp_img:
 * @width: the width of the image
 * @height: the height of the image
 * @length: the length of the data associated with the image
 * @flags: @FpiImgFlags flags describing the image contained in the structure
 * @minutiae: an opaque structure representing the detected minutiae
 * @binarized: the binarized image data
 * @data: the start of the image data, which will be of @length size.
 *
 * A structure representing a captured, or processed image. The @flags member
 * will show its current state, including whether whether the binarized form
 * if present, whether it is complete, and whether it needs particular changes
 * before being processed.
 */
struct fp_img {
	int width;
	int height;
	size_t length;
	FpiImgFlags flags;
	/*< private >*/
	struct fp_minutiae *minutiae;
	/*< public >*/
	unsigned char *binarized;
	unsigned char data[0];
};

struct fp_img *fpi_img_new(size_t length);
struct fp_img *fpi_img_new_for_imgdev(struct fp_img_dev *imgdev);
struct fp_img *fpi_img_realloc(struct fp_img *img, size_t newsize);
struct fp_img *fpi_img_resize(struct fp_img *img, unsigned int w_factor, unsigned int h_factor);

int fpi_std_sq_dev(const unsigned char *buf, int size);
int fpi_mean_sq_diff_norm(unsigned char *buf1, unsigned char *buf2, int size);

#endif
