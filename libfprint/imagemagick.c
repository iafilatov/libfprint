/*
 * Imaging utility functions for libfprint
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

#include <errno.h>
#include <magick/ImageMagick.h>

#include "fp_internal.h"

struct fp_img *fpi_im_resize(struct fp_img *img, unsigned int w_factor, unsigned int h_factor)
{
	Image *mimg;
	Image *resized;
	ExceptionInfo exception;
	MagickBooleanType ret;
	int new_width = img->width * w_factor;
	int new_height = img->height * h_factor;
	struct fp_img *newimg;

	/* It is possible to implement resizing using a simple algorithm, however
	 * we use ImageMagick because it applies some kind of smoothing to the
	 * result, which improves matching performances in my experiments. */

	if (!IsMagickInstantiated())
		InitializeMagick(NULL);
	
	GetExceptionInfo(&exception);
	mimg = ConstituteImage(img->width, img->height, "I", CharPixel, img->data,
		&exception);

	GetExceptionInfo(&exception);
	resized = ResizeImage(mimg, new_width, new_height, 0, 1.0, &exception);

	newimg = fpi_img_new(new_width * new_height);
	newimg->width = new_width;
	newimg->height = new_height;
	newimg->flags = img->flags;

	GetExceptionInfo(&exception);
	ret = ExportImagePixels(resized, 0, 0, new_width, new_height, "I",
		CharPixel, newimg->data, &exception);
	if (ret != MagickTrue) {
		fp_err("export failed");
		return NULL;
	}

	DestroyImage(mimg);
	DestroyImage(resized);

	return newimg;
}

