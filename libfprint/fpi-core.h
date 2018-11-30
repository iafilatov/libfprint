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

#ifndef __FPI_CORE_H__
#define __FPI_CORE_H__

#include <fprint.h>
#include "fpi-dev-img.h"

/**
 * usb_id:
 * @vendor: the USB vendor ID
 * @product: the USB product ID
 * @driver_data: data to differentiate devices of different
 *   vendor and product IDs.
 *
 * The struct #usb_id is used to declare devices supported by a
 * particular driver. The @driver_data information is used to
 * differentiate different models of devices which only need
 * small changes compared to the default driver behaviour to function.
 *
 * For example, a device might have a different initialisation from
 * the stock device, so the driver could do:
 *
 * |[<!-- language="C" -->
 *    if (driver_data == MY_DIFFERENT_DEVICE_QUIRK) {
 *        ...
 *    } else {
 *        ...
 *    }
 * ]|
 *
 * The default value is zero, so the @driver_data needs to be a
 * non-zero to be useful.
 */
struct usb_id {
	uint16_t vendor;
	uint16_t product;
	unsigned long driver_data;
};

/**
 * fp_driver_type:
 * @DRIVER_PRIMITIVE: primitive, non-imaging, driver
 * @DRIVER_IMAGING: imaging driver
 *
 * The type of device the driver supports.
 */
enum fp_driver_type {
	DRIVER_PRIMITIVE = 0,
	DRIVER_IMAGING = 1,
};

struct fp_driver {
	const uint16_t id;
	const char *name;
	const char *full_name;
	const struct usb_id * const id_table;
	enum fp_driver_type type;
	enum fp_scan_type scan_type;

	/* Device operations */
	int (*discover)(struct libusb_device_descriptor *dsc, uint32_t *devtype);
	int (*open)(struct fp_dev *dev, unsigned long driver_data);
	void (*close)(struct fp_dev *dev);
	int (*enroll_start)(struct fp_dev *dev);
	int (*enroll_stop)(struct fp_dev *dev);
	int (*verify_start)(struct fp_dev *dev);
	int (*verify_stop)(struct fp_dev *dev, gboolean iterating);
	int (*identify_start)(struct fp_dev *dev);
	int (*identify_stop)(struct fp_dev *dev, gboolean iterating);
	int (*capture_start)(struct fp_dev *dev);
	int (*capture_stop)(struct fp_dev *dev);
};

/**
 * FpiImgDriverFlags:
 * @FP_IMGDRV_SUPPORTS_UNCONDITIONAL_CAPTURE: Whether the driver supports
 *   unconditional image capture. No driver currently does.
 *
 * Flags used in the #fp_img_driver to advertise the capabilities of drivers.
 */
typedef enum {
	FP_IMGDRV_SUPPORTS_UNCONDITIONAL_CAPTURE = 1 << 0
} FpiImgDriverFlags;

struct fp_img_driver {
	struct fp_driver driver;
	FpiImgDriverFlags flags;
	int img_width;
	int img_height;
	int bz3_threshold;

	/* Device operations */
	int (*open)(struct fp_img_dev *dev, unsigned long driver_data);
	void (*close)(struct fp_img_dev *dev);
	int (*activate)(struct fp_img_dev *dev, enum fp_imgdev_state state);
	int (*change_state)(struct fp_img_dev *dev, enum fp_imgdev_state state);
	void (*deactivate)(struct fp_img_dev *dev);
};

#endif
