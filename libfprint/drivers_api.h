/*
 * Driver API definitions
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

#ifndef __DRIVERS_API_H__
#define __DRIVERS_API_H__

#include <config.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <libusb.h>

#include "fprint.h"
#include "fpi-log.h"
#include "fpi-ssm.h"
#include "fpi-poll.h"
#include "fpi-dev.h"
#include "assembling.h"
#include "drivers/driver_ids.h"

libusb_device_handle *fpi_dev_get_usb_dev(struct fp_dev *dev);
void fpi_dev_set_nr_enroll_stages(struct fp_dev *dev, int nr_enroll_stages);
struct fp_print_data *fpi_dev_get_verify_data(struct fp_dev *dev);

enum fp_imgdev_state {
	IMGDEV_STATE_INACTIVE,
	IMGDEV_STATE_AWAIT_FINGER_ON,
	IMGDEV_STATE_CAPTURE,
	IMGDEV_STATE_AWAIT_FINGER_OFF,
};

enum fp_imgdev_action {
	IMG_ACTION_NONE = 0,
	IMG_ACTION_ENROLL,
	IMG_ACTION_VERIFY,
	IMG_ACTION_IDENTIFY,
	IMG_ACTION_CAPTURE,
};

enum fp_imgdev_enroll_state {
	IMG_ACQUIRE_STATE_NONE = 0,
	IMG_ACQUIRE_STATE_ACTIVATING,
	IMG_ACQUIRE_STATE_AWAIT_FINGER_ON,
	IMG_ACQUIRE_STATE_AWAIT_IMAGE,
	IMG_ACQUIRE_STATE_AWAIT_FINGER_OFF,
	IMG_ACQUIRE_STATE_DONE,
	IMG_ACQUIRE_STATE_DEACTIVATING,
};

enum fp_imgdev_enroll_state fpi_imgdev_get_action_state(struct fp_img_dev *imgdev);
enum fp_imgdev_action fpi_imgdev_get_action(struct fp_img_dev *imgdev);
int fpi_imgdev_get_action_result(struct fp_img_dev *imgdev);
void fpi_imgdev_set_action_result(struct fp_img_dev *imgdev, int action_result);
int fpi_imgdev_get_img_width(struct fp_img_dev *imgdev);
int fpi_imgdev_get_img_height(struct fp_img_dev *imgdev);

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

/* flags for fp_img_driver.flags */
#define FP_IMGDRV_SUPPORTS_UNCONDITIONAL_CAPTURE (1 << 0)

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

enum fp_print_data_type {
	PRINT_DATA_RAW = 0, /* memset-imposed default */
	PRINT_DATA_NBIS_MINUTIAE,
};

struct fp_print_data_item {
	size_t length;
	unsigned char data[0];
};

struct fp_print_data {
	uint16_t driver_id;
	uint32_t devtype;
	enum fp_print_data_type type;
	GSList *prints;
};

struct fp_print_data *fpi_print_data_new(struct fp_dev *dev);
struct fp_print_data_item *fpi_print_data_item_new(size_t length);
gboolean fpi_print_data_compatible(uint16_t driver_id1, uint32_t devtype1,
	enum fp_print_data_type type1, uint16_t driver_id2, uint32_t devtype2,
	enum fp_print_data_type type2);

struct fp_minutiae;

/* bit values for fp_img.flags */
#define FP_IMG_V_FLIPPED	(1<<0)
#define FP_IMG_H_FLIPPED	(1<<1)
#define FP_IMG_COLORS_INVERTED	(1<<2)
#define FP_IMG_BINARIZED_FORM	(1<<3)
#define FP_IMG_PARTIAL		(1<<4)

#define FP_IMG_STANDARDIZATION_FLAGS (FP_IMG_V_FLIPPED | FP_IMG_H_FLIPPED \
	| FP_IMG_COLORS_INVERTED)

struct fp_img {
	int width;
	int height;
	size_t length;
	uint16_t flags;
	struct fp_minutiae *minutiae;
	unsigned char *binarized;
	unsigned char data[0];
};

struct fp_img *fpi_img_new(size_t length);
struct fp_img *fpi_img_new_for_imgdev(struct fp_img_dev *dev);
struct fp_img *fpi_img_resize(struct fp_img *img, size_t newsize);
struct fp_img *fpi_im_resize(struct fp_img *img, unsigned int w_factor, unsigned int h_factor);

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

void fpi_drvcb_identify_started(struct fp_dev *dev, int status);
void fpi_drvcb_report_identify_result(struct fp_dev *dev, int result,
	size_t match_offset, struct fp_img *img);
void fpi_drvcb_identify_stopped(struct fp_dev *dev);

void fpi_drvcb_capture_started(struct fp_dev *dev, int status);
void fpi_drvcb_report_capture_result(struct fp_dev *dev, int result,
	struct fp_img *img);
void fpi_drvcb_capture_stopped(struct fp_dev *dev);

/* for image drivers */
void fpi_imgdev_open_complete(struct fp_img_dev *imgdev, int status);
void fpi_imgdev_close_complete(struct fp_img_dev *imgdev);
void fpi_imgdev_activate_complete(struct fp_img_dev *imgdev, int status);
void fpi_imgdev_deactivate_complete(struct fp_img_dev *imgdev);
void fpi_imgdev_report_finger_status(struct fp_img_dev *imgdev,
	gboolean present);
void fpi_imgdev_image_captured(struct fp_img_dev *imgdev, struct fp_img *img);
void fpi_imgdev_abort_scan(struct fp_img_dev *imgdev, int result);
void fpi_imgdev_session_error(struct fp_img_dev *imgdev, int error);

/* utils */
int fpi_std_sq_dev(const unsigned char *buf, int size);
int fpi_mean_sq_diff_norm(unsigned char *buf1, unsigned char *buf2, int size);

#endif

