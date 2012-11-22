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
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "fp_internal.h"

struct fp_img *fpi_im_resize(struct fp_img *img, unsigned int w_factor, unsigned int h_factor)
{
	int new_width = img->width * w_factor;
	int new_height = img->height * h_factor;
	GdkPixbuf *orig, *resized;
	struct fp_img *newimg;
	guchar *pixels;
	guint y;
	int rowstride;

	g_type_init ();

	/* It is possible to implement resizing using a simple algorithm, however
	 * we use gdk-pixbuf because it applies some kind of smoothing to the
	 * result, which improves matching performances in my experiments. */

	/* Create the original pixbuf, and fill it in from the grayscale data */
	orig = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
			       FALSE,
			       8,
			       img->width,
			       img->height);
	rowstride = gdk_pixbuf_get_rowstride (orig);
	pixels = gdk_pixbuf_get_pixels (orig);
	for (y = 0; y < img->height; y++) {
		guint x;
		for (x = 0; x < img->width; x++) {
			guchar *p, *r;

			p = pixels + y * rowstride + x * 3;
			r = img->data + y * img->width + x;
			p[0] = r[0];
			p[1] = r[0];
			p[2] = r[0];
		}
	}

	/* Resize the pixbuf, and create the new fp_img */
	resized = gdk_pixbuf_scale_simple (orig, new_width, new_height, GDK_INTERP_HYPER);
	g_object_unref (orig);

	newimg = fpi_img_new(new_width * new_height);
	newimg->width = new_width;
	newimg->height = new_height;
	newimg->flags = img->flags;

	rowstride = gdk_pixbuf_get_rowstride (resized);
	pixels = gdk_pixbuf_get_pixels (resized);
	for (y = 0; y < newimg->height; y++) {
		guint x;
		for (x = 0; x < newimg->width; x++) {
			guchar *p, *r;

			r = newimg->data + y * newimg->width + x;
			p = pixels + y * rowstride + x * 3;
			r[0] = p[0];
		}
	}

	g_object_unref (resized);

	return newimg;
}

