/*
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2015 Vasily Khoruzhick <anarsoul@gmail.com>
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

#ifndef __FPI_ASSEMBLING_H__
#define __FPI_ASSEMBLING_H__

#include <fprint.h>

struct fpi_frame {
	int delta_x;
	int delta_y;
	unsigned char data[0];
};

struct fpi_frame_asmbl_ctx {
	unsigned int frame_width;
	unsigned int frame_height;
	unsigned int image_width;
	unsigned char (*get_pixel)(struct fpi_frame_asmbl_ctx *ctx,
				   struct fpi_frame *frame,
				   unsigned int x,
				   unsigned int y);
};

void fpi_do_movement_estimation(struct fpi_frame_asmbl_ctx *ctx,
			    GSList *stripes, size_t stripes_len);

struct fp_img *fpi_assemble_frames(struct fpi_frame_asmbl_ctx *ctx,
			    GSList *stripes, size_t stripes_len);

struct fpi_line_asmbl_ctx {
	unsigned int line_width;
	unsigned int max_height;
	unsigned int resolution;
	unsigned int median_filter_size;
	unsigned int max_search_offset;
	int (*get_deviation)(struct fpi_line_asmbl_ctx *ctx,
			     GSList *line1, GSList *line2);
	unsigned char (*get_pixel)(struct fpi_line_asmbl_ctx *ctx,
				   GSList *line,
				   unsigned int x);
};

struct fp_img *fpi_assemble_lines(struct fpi_line_asmbl_ctx *ctx,
				  GSList *lines, size_t lines_len);

#endif
