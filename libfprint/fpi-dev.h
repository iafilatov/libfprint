/*
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

#ifndef __FPI_DEV_H__
#define __FPI_DEV_H__

#include <libusb.h>
#include <fprint.h>

struct fp_dev;

/**
 * fp_img_dev:
 *
 * #fp_img_dev is an opaque structure type. You must access it using the
 * appropriate functions.
 */
struct fp_img_dev;

struct fp_dev           *FP_DEV           (struct fp_img_dev *dev);
struct fp_img_dev       *FP_IMG_DEV       (struct fp_dev *dev);

void                     fp_dev_set_instance_data (struct fp_dev *dev,
						   void          *instance_data);
void                    *FP_INSTANCE_DATA         (struct fp_dev *dev);

libusb_device_handle *fpi_dev_get_usb_dev(struct fp_dev *dev);
void fpi_dev_set_nr_enroll_stages(struct fp_dev *dev,
				  int nr_enroll_stages);
struct fp_print_data *fpi_dev_get_verify_data(struct fp_dev *dev);

#endif
