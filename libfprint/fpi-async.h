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

#ifndef __FPI_ASYNC_H__
#define __FPI_ASYNC_H__

#include "fpi-dev.h"
#include "fpi-data.h"

void fpi_drvcb_open_complete(struct fp_dev *dev, int status);
void fpi_drvcb_close_complete(struct fp_dev *dev);

void fpi_drvcb_enroll_started(struct fp_dev *dev, int status);
void fpi_drvcb_enroll_stage_completed(struct fp_dev *dev, int result,
	struct fp_print_data *data, struct fp_img *img);
void fpi_drvcb_enroll_stopped(struct fp_dev *dev);

void fpi_drvcb_verify_started(struct fp_dev *dev, int status);
void fpi_drvcb_report_verify_result(struct fp_dev *dev, int result,
	struct fp_img *img);
void fpi_drvcb_verify_stopped(struct fp_dev *dev);

#endif
