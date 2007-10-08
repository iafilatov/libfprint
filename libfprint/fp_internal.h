/*
 * Internal/private definitions for libfprint
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

#ifndef __FPRINT_INTERNAL_H__
#define __FPRINT_INTERNAL_H__

#include <stdint.h>

#include <usb.h>

#include <fprint.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

struct usb_id {
	uint16_t vendor;
	uint16_t product;
	unsigned long driver_data;
};

struct fp_driver {
	const char *name;
	const char *full_name;
	const struct usb_id * const id_table;
};

extern const struct fp_driver upekts_driver;

struct fp_dscv_dev {
	struct usb_device *udev;
	const struct fp_driver *drv;
};

#endif

