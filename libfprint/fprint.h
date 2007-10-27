/*
 * Main definitions for libfprint
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

#ifndef __FPRINT_H__
#define __FPRINT_H__

/* structs that applications are not allowed to peek into */
struct fp_dscv_dev;
struct fp_dev;
struct fp_driver;
struct fp_print_data;
struct fp_img;

/* misc/general stuff */
enum fp_finger {
	LEFT_THUMB = 1,
	LEFT_INDEX,
	LEFT_MIDDLE,
	LEFT_RING,
	LEFT_LITTLE,
	RIGHT_THUMB,
	RIGHT_INDEX,
	RIGHT_MIDDLE,
	RIGHT_RING,
	RIGHT_LITTLE,
};

/* Device discovery */
struct fp_dscv_dev **fp_discover_devs(void);
void fp_dscv_devs_free(struct fp_dscv_dev **devs);
struct fp_driver *fp_dscv_dev_get_driver(struct fp_dscv_dev *dev);

/* Device handling */
struct fp_dev *fp_dev_open(struct fp_dscv_dev *ddev);
void fp_dev_close(struct fp_dev *dev);
struct fp_driver *fp_dev_get_driver(struct fp_dev *dev);
int fp_dev_get_nr_enroll_stages(struct fp_dev *dev);
struct fp_img_dev *fp_dev_to_img_dev(struct fp_dev *dev);

/* Drivers */
const char *fp_driver_get_name(struct fp_driver *drv);
const char *fp_driver_get_full_name(struct fp_driver *drv);

/* Enrollment */
enum fp_enroll_result {
	FP_ENROLL_COMPLETE = 1,
	FP_ENROLL_FAIL,
	FP_ENROLL_PASS,
	FP_ENROLL_RETRY = 100,
	FP_ENROLL_RETRY_TOO_SHORT,
	FP_ENROLL_RETRY_CENTER_FINGER,
	FP_ENROLL_RETRY_REMOVE_FINGER,
};

int fp_enroll_finger(struct fp_dev *dev, struct fp_print_data **print_data);

/* Verification */
enum fp_verify_result {
	FP_VERIFY_NO_MATCH = 0,
	FP_VERIFY_MATCH = 1,
	FP_VERIFY_RETRY = FP_ENROLL_RETRY,
	FP_VERIFY_RETRY_TOO_SHORT = FP_ENROLL_RETRY_TOO_SHORT,
	FP_VERIFY_RETRY_CENTER_FINGER = FP_ENROLL_RETRY_CENTER_FINGER,
	FP_VERIFY_RETRY_REMOVE_FINGER = FP_ENROLL_RETRY_REMOVE_FINGER,
};

int fp_verify_finger(struct fp_dev *dev, struct fp_print_data *enrolled_print);

/* Data handling */
int fp_print_data_load(struct fp_dev *dev, enum fp_finger finger,
	struct fp_print_data **data);
int fp_print_data_save(struct fp_print_data *data, enum fp_finger finger);
void fp_print_data_free(struct fp_print_data *data);

/* Imaging devices */
int fp_imgdev_capture(struct fp_img_dev *imgdev, int unconditional,
	struct fp_img **image);

/* Image handling */
int fp_img_get_height(struct fp_img *img);
int fp_img_get_width(struct fp_img *img);
unsigned char *fp_img_get_data(struct fp_img *img);
int fp_img_save_to_file(struct fp_img *img, char *path);
void fp_img_standardize(struct fp_img *img);

/* Library */
int fp_init(void);

#endif

