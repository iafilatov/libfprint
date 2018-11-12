/*
 * Internal/private definitions for libfprint
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

#ifndef __FPRINT_INTERNAL_H__
#define __FPRINT_INTERNAL_H__

#include <config.h>

#include <stdint.h>
#include <errno.h>
#include <glib.h>
#include <libusb.h>

#include "fprint.h"
#include "fpi-log.h"
#include "fpi-dev.h"
#include "fpi-dev-img.h"
#include "fpi-data.h"
#include "fpi-img.h"
#include "drivers/driver_ids.h"

/* Global variables */
extern libusb_context *fpi_usb_ctx;
extern GSList *opened_devices;

/* fp_dev structure definition */
enum fp_dev_state {
	DEV_STATE_INITIAL = 0,
	DEV_STATE_ERROR,
	DEV_STATE_INITIALIZING,
	DEV_STATE_INITIALIZED,
	DEV_STATE_DEINITIALIZING,
	DEV_STATE_DEINITIALIZED,
	DEV_STATE_ENROLL_STARTING,
	DEV_STATE_ENROLLING,
	DEV_STATE_ENROLL_STOPPING,
	DEV_STATE_VERIFY_STARTING,
	DEV_STATE_VERIFYING,
	DEV_STATE_VERIFY_DONE,
	DEV_STATE_VERIFY_STOPPING,
	DEV_STATE_IDENTIFY_STARTING,
	DEV_STATE_IDENTIFYING,
	DEV_STATE_IDENTIFY_DONE,
	DEV_STATE_IDENTIFY_STOPPING,
	DEV_STATE_CAPTURE_STARTING,
	DEV_STATE_CAPTURING,
	DEV_STATE_CAPTURE_DONE,
	DEV_STATE_CAPTURE_STOPPING,
};

struct fp_dev {
	struct fp_driver *drv;
	uint32_t devtype;

	/* only valid if drv->type == DRIVER_IMAGING */
	struct fp_img_dev *img_dev;
	/* Link to the instance specific struct */
	void *instance_data;

	int nr_enroll_stages;

	/* FIXME: This will eventually have a bus type */
	libusb_device_handle *udev;

	/* read-only to drivers */
	struct fp_print_data *verify_data;

	/* drivers should not mess with any of the below */
	enum fp_dev_state state;
	int __enroll_stage;
	int unconditional_capture;

	/* async I/O callbacks and data */
	/* FIXME: convert this to generic state operational data mechanism? */
	fp_dev_open_cb open_cb;
	void *open_cb_data;
	fp_operation_stop_cb close_cb;
	void *close_cb_data;
	fp_enroll_stage_cb enroll_stage_cb;
	void *enroll_stage_cb_data;
	fp_operation_stop_cb enroll_stop_cb;
	void *enroll_stop_cb_data;
	fp_img_operation_cb verify_cb;
	void *verify_cb_data;
	fp_operation_stop_cb verify_stop_cb;
	void *verify_stop_cb_data;
	fp_identify_cb identify_cb;
	void *identify_cb_data;
	fp_operation_stop_cb identify_stop_cb;
	void *identify_stop_cb_data;
	fp_img_operation_cb capture_cb;
	void *capture_cb_data;
	fp_operation_stop_cb capture_stop_cb;
	void *capture_stop_cb_data;

	/* FIXME: better place to put this? */
	struct fp_print_data **identify_gallery;
};

/* fp_img_dev structure definition */
struct fp_img_dev {
	struct fp_dev *parent;

	enum fp_imgdev_action action;
	int action_state;

	struct fp_print_data *acquire_data;
	struct fp_print_data *enroll_data;
	struct fp_img *acquire_img;
	int enroll_stage;
	int action_result;

	/* FIXME: better place to put this? */
	size_t identify_match_offset;
};

/* fp_driver structure definition */

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

/* fp_img_driver structure definition */
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define fpi_driver_to_img_driver(drv) \
	container_of((drv), struct fp_img_driver, driver)

struct fp_img_driver {
	struct fp_driver driver;
	uint16_t flags;
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

/* fp_dscv_dev structure definition */
struct fp_dscv_dev {
	struct libusb_device *udev;
	struct fp_driver *drv;
	unsigned long driver_data;
	uint32_t devtype;
};

/* fp_dscv_print structure definition */
struct fp_dscv_print {
	uint16_t driver_id;
	uint32_t devtype;
	enum fp_finger finger;
	char *path;
};

/* fp_minutia structure definition */
struct fp_minutia {
	int x;
	int y;
	int ex;
	int ey;
	int direction;
	double reliability;
	int type;
	int appearing;
	int feature_id;
	int *nbrs;
	int *ridge_counts;
	int num_nbrs;
};

/* fp_minutiae structure definition */
struct fp_minutiae {
	int alloc;
	int num;
	struct fp_minutia **list;
};

/* Defined in fpi-dev-img.c */
void fpi_img_driver_setup(struct fp_img_driver *idriver);
int fpi_imgdev_get_img_width(struct fp_img_dev *imgdev);
int fpi_imgdev_get_img_height(struct fp_img_dev *imgdev);

/* Exported for use in command-line tools
 * Defined in fpi-core.c */
struct fp_driver **fprint_get_drivers (void);

/* Defined in fpi-core.c */
enum fp_print_data_type fpi_driver_get_data_type(struct fp_driver *drv);

/* Defined in fpi-data.c */
void fpi_data_exit(void);
gboolean fpi_print_data_compatible(uint16_t driver_id1, uint32_t devtype1,
	enum fp_print_data_type type1, uint16_t driver_id2, uint32_t devtype2,
	enum fp_print_data_type type2);

/* Defined in fpi-img.c */
gboolean fpi_img_is_sane(struct fp_img *img);
int fpi_img_to_print_data(struct fp_img_dev *imgdev, struct fp_img *img,
	struct fp_print_data **ret);
int fpi_img_compare_print_data(struct fp_print_data *enrolled_print,
	struct fp_print_data *new_print);
int fpi_img_compare_print_data_to_gallery(struct fp_print_data *print,
	struct fp_print_data **gallery, int match_threshold, size_t *match_offset);

/* Defined in fpi-poll.c */
void fpi_timeout_cancel_all_for_dev(struct fp_dev *dev);
void fpi_poll_init(void);
void fpi_poll_exit(void);

#include "drivers_definitions.h"

#endif
