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

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

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

struct fp_img_dev {
	struct fp_dev *dev;
	usb_dev_handle *udev;
	void *priv;
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
	int (*init)(struct fp_dev *dev, unsigned long driver_data);
	void (*exit)(struct fp_dev *dev);
	int (*enroll)(struct fp_dev *dev, gboolean initial, int stage,
		struct fp_print_data **print_data);
	int (*verify)(struct fp_dev *dev, struct fp_print_data *data);
};

/* flags for fp_img_driver.flags */
#define FP_IMGDRV_SUPPORTS_UNCONDITIONAL_CAPTURE (1 << 0)

struct fp_img_driver {
	struct fp_driver driver;
	uint16_t flags;
	int img_width;
	int img_height;

	/* Device operations */
	int (*init)(struct fp_img_dev *dev, unsigned long driver_data);
	void (*exit)(struct fp_img_dev *dev);
	int (*await_finger_on)(struct fp_img_dev *dev);
	int (*await_finger_off)(struct fp_img_dev *dev);
	int (*capture)(struct fp_img_dev *dev, gboolean unconditional,
		struct fp_img **image);
};

extern struct fp_driver upekts_driver;
extern struct fp_img_driver uru4000_driver;
extern struct fp_img_driver aes4000_driver;

void fpi_img_driver_setup(struct fp_img_driver *idriver);

#define fpi_driver_to_img_driver(drv) \
	container_of((drv), struct fp_img_driver, driver)

struct fp_dscv_dev {
	struct usb_device *udev;
	struct fp_driver *drv;
	unsigned long driver_data;
};

struct fp_print_data {
	const char *driver_name;
	size_t length;
	unsigned char buffer[0];
};

struct fp_print_data *fpi_print_data_new(struct fp_dev *dev, size_t length);
int fpi_print_data_compatible(struct fp_dev *dev, struct fp_print_data *data);

/* bit values for fp_img.flags */
#define FP_IMG_V_FLIPPED 		(1<<0)
#define FP_IMG_H_FLIPPED 		(1<<1)
#define FP_IMG_COLORS_INVERTED	(1<<2)

struct fp_img {
	int width;
	int height;
	size_t length;
	uint16_t flags;
	unsigned char data[0];
};

struct fp_img *fpi_img_new(size_t length);
struct fp_img *fpi_img_new_for_imgdev(struct fp_img_dev *dev);
struct fp_img *fpi_img_resize(struct fp_img *img, size_t newsize);
gboolean fpi_img_is_sane(struct fp_img *img);

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

