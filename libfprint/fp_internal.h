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

#include <config.h>

#include <stdint.h>

#include <glib.h>
#include <usb.h>

#ifdef __DARWIN_NULL
/* Darwin does endianness slightly differently */
#include <machine/endian.h>
#define __BYTE_ORDER BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#else /* Linux */
#include <endian.h>
#endif

#include <fprint.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

enum fpi_log_level {
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_ERROR,
};

void fpi_log(enum fpi_log_level, const char *component, const char *function,
	const char *format, ...);

#ifndef FP_COMPONENT
#define FP_COMPONENT NULL
#endif

#ifdef ENABLE_LOGGING
#define _fpi_log(level, fmt...) fpi_log(level, FP_COMPONENT, __FUNCTION__, fmt)
#else
#define _fpi_log(level, fmt...)
#endif

#ifdef ENABLE_DEBUG_LOGGING
#define fp_dbg(fmt...) _fpi_log(LOG_LEVEL_DEBUG, fmt)
#else
#define fp_dbg(fmt...)
#endif

#define fp_info(fmt...) _fpi_log(LOG_LEVEL_INFO, fmt)
#define fp_warn(fmt...) _fpi_log(LOG_LEVEL_WARNING, fmt)
#define fp_err(fmt...) _fpi_log(LOG_LEVEL_ERROR, fmt)

struct fp_dev {
	struct fp_driver *drv;
	usb_dev_handle *udev;
	void *priv;

	int nr_enroll_stages;

	/* drivers should not mess with these */
	int __enroll_stage;
};

struct usb_id {
	uint16_t vendor;
	uint16_t product;
	unsigned long driver_data;
};

enum fp_driver_type {
	DRIVER_PRIMITIVE = 0,
	DRIVER_IMAGING = 1,
};

struct fp_driver {
	const char *name;
	const char *full_name;
	const struct usb_id * const id_table;
	enum fp_driver_type type;

	void *priv;

	/* Device operations */
	int (*init)(struct fp_dev *dev);
	void (*exit)(struct fp_dev *dev);
	int (*enroll)(struct fp_dev *dev, gboolean initial, int stage,
		struct fp_print_data **print_data);
	int (*verify)(struct fp_dev *dev, struct fp_print_data *data);
};

struct fp_img_driver {
	struct fp_driver driver;

	/* Device operations */
};

extern struct fp_driver upekts_driver;
extern struct fp_img_driver uru4000_driver;

void fpi_img_driver_setup(struct fp_img_driver *idriver);

struct fp_dscv_dev {
	struct usb_device *udev;
	struct fp_driver *drv;
};

struct fp_print_data {
	const char *driver_name;
	size_t length;
	unsigned char buffer[0];
};

struct fp_print_data *fpi_print_data_new(struct fp_dev *dev, size_t length);
unsigned char *fpi_print_data_get_buffer(struct fp_print_data *data);
int fpi_print_data_compatible(struct fp_dev *dev, struct fp_print_data *data);

#define bswap16(x) (((x & 0xff) << 8) | (x >> 8))
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define cpu_to_le16(x) (x)
#define le16_to_cpu(x) (x)
#define cpu_to_be16(x) bswap16(x)
#define be16_to_cpu(x) bswap16(x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define cpu_to_le16(x) bswap16(x)
#define le16_to_cpu(x) bswap16(x)
#define cpu_to_be16(x) (x)
#define be16_to_cpu(x) (x)
#else
#error "Unrecognized endianness"
#endif

#endif

