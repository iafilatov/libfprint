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

/**
 * fpi_frame:
 * @delta_x: X offset of the frame
 * @delta_y: Y offset of the frame
 * @data: bitmap
 *
 * #fpi_frame is used to store frames for swipe sensors. Drivers should
 * populate delta_x and delta_y if the device supports hardware movement
 * estimation.
 */
struct fpi_frame {
	int delta_x;
	int delta_y;
	unsigned char data[0];
};

/**
 * fpi_frame_asmbl_ctx:
 * @frame_width: width of the frame
 * @frame_height: height of the frame
 * @image_width: resulting image width
 * @get_pixel: pixel accessor, returns pixel brightness at x,y of frame
 *
 * #fpi_frame_asmbl_ctx is a structure holding the context for frame
 * assembling routines.
 *
 * Drivers should define their own #fpi_frame_asmbl_ctx depending on
 * hardware parameters of scanner. @image_width is usually 25% wider than
 * @frame_width to take horizontal movement into account.
 */
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
			    GSList *stripes, size_t num_stripes);

struct fp_img *fpi_assemble_frames(struct fpi_frame_asmbl_ctx *ctx,
			    GSList *stripes, size_t num_stripes);

/**
 * fpi_line_asmbl_ctx:
 * @line_width: width of line
 * @max_height: maximal height of assembled image
 * @resolution: scale factor used for line assembling routines.
 * @median_filter_size: size of median filter for movement estimation
 * @max_search_offset: the number of lines to search for the next line
 * @get_deviation: pointer to a function that returns the numerical difference
 *                 between two lines
 * @get_pixel: pixel accessor, returns pixel brightness at x of line
 *
 * #fpi_line_asmbl_ctx is a structure holding the context for line assembling
 * routines.
 *
 * Drivers should define their own #fpi_line_asmbl_ctx depending on
 * the hardware parameters of the scanner. Swipe scanners of this type usually
 * return two lines, the second line is often narrower than first and is used
 * for movement estimation.
 *
 * The @max_search_offset value indicates how many lines forward the assembling
 * routines should look while searching for next line. This value depends on
 * how fast the hardware sends frames.
 *
 * The function pointed to by @get_deviation should return the numerical difference
 * between two lines. Higher values means lines are more different. If the reader
 * returns two lines at a time, this function should be used to estimate the
 * difference between pairs of lines.
 */
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
				  GSList *lines, size_t num_lines);

#endif
