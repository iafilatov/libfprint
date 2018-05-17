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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/time.h>

/**
 * fp_dscv_dev:
 *
 */
struct fp_dscv_dev;

/**
 * fp_dscv_print:
 *
 */
struct fp_dscv_print;

/**
 * fp_dev:
 *
 */
struct fp_dev;

/**
 * fp_driver:
 *
 */
struct fp_driver;

/**
 * fp_print_data:
 *
 */
struct fp_print_data;

/**
 * fp_img:
 *
 */
struct fp_img;

/* misc/general stuff */

/**
 * fp_finger:
 * @LEFT_THUMB: Left thumb
 * @LEFT_INDEX: Left index finger
 * @LEFT_MIDDLE: Left middle finger
 * @LEFT_RING: Left ring finger
 * @LEFT_LITTLE: Left little finger
 * @RIGHT_THUMB: Right thumb
 * @RIGHT_INDEX: Right index finger
 * @RIGHT_MIDDLE: Right middle finger
 * @RIGHT_RING: Right ring finger
 * @RIGHT_LITTLE: Right little finger
 *
 * Numeric codes used to refer to fingers (and thumbs) of a human. These are
 * purposely not available as strings, to avoid getting the library tangled up
 * in localization efforts.
 */
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

/**
 * fp_scan_type:
 * @FP_SCAN_TYPE_PRESS: the reader has a surface area that covers the whole finger
 * @FP_SCAN_TYPE_SWIPE: the reader requires swiping the finger on a smaller area
 *
 * Numeric codes used to refer to the scan type of the device. Devices require
 * either swiping or pressing the finger on the device. This is useful for
 * front-ends.
 */
enum fp_scan_type {
	FP_SCAN_TYPE_PRESS = 0,
	FP_SCAN_TYPE_SWIPE,
};

/* Drivers */
const char *fp_driver_get_name(struct fp_driver *drv);
const char *fp_driver_get_full_name(struct fp_driver *drv);
uint16_t fp_driver_get_driver_id(struct fp_driver *drv);
enum fp_scan_type fp_driver_get_scan_type(struct fp_driver *drv);

/* Device discovery */
struct fp_dscv_dev **fp_discover_devs(void);
void fp_dscv_devs_free(struct fp_dscv_dev **devs);
struct fp_driver *fp_dscv_dev_get_driver(struct fp_dscv_dev *dev);
uint32_t fp_dscv_dev_get_devtype(struct fp_dscv_dev *dev);
int fp_dscv_dev_supports_print_data(struct fp_dscv_dev *dev,
	struct fp_print_data *print);
int fp_dscv_dev_supports_dscv_print(struct fp_dscv_dev *dev,
	struct fp_dscv_print *print);
struct fp_dscv_dev *fp_dscv_dev_for_print_data(struct fp_dscv_dev **devs,
	struct fp_print_data *print);
struct fp_dscv_dev *fp_dscv_dev_for_dscv_print(struct fp_dscv_dev **devs,
	struct fp_dscv_print *print);

/**
 * fp_dscv_dev_get_driver_id:
 * @dev: a discovered fingerprint device
 *
 * Returns: the ID for the underlying driver for that device
 */
static inline uint16_t fp_dscv_dev_get_driver_id(struct fp_dscv_dev *dev)
{
	return fp_driver_get_driver_id(fp_dscv_dev_get_driver(dev));
}

/* Print discovery */
struct fp_dscv_print **fp_discover_prints(void);
void fp_dscv_prints_free(struct fp_dscv_print **prints);
uint16_t fp_dscv_print_get_driver_id(struct fp_dscv_print *print);
uint32_t fp_dscv_print_get_devtype(struct fp_dscv_print *print);
enum fp_finger fp_dscv_print_get_finger(struct fp_dscv_print *print);
int fp_dscv_print_delete(struct fp_dscv_print *print);

/* Device handling */
struct fp_dev *fp_dev_open(struct fp_dscv_dev *ddev);
void fp_dev_close(struct fp_dev *dev);
struct fp_driver *fp_dev_get_driver(struct fp_dev *dev);
int fp_dev_get_nr_enroll_stages(struct fp_dev *dev);
uint32_t fp_dev_get_devtype(struct fp_dev *dev);
int fp_dev_supports_print_data(struct fp_dev *dev, struct fp_print_data *data);
int fp_dev_supports_dscv_print(struct fp_dev *dev, struct fp_dscv_print *print);

/**
 * fp_capture_result:
 * Whether a capture failed or completed.
 *
 * @FP_CAPTURE_COMPLETE: Capture completed successfully, the capture data has been returned to the caller.
 * @FP_CAPTURE_FAIL: Capture failed
 *
 */
enum fp_capture_result {
	FP_CAPTURE_COMPLETE = 0,
	FP_CAPTURE_FAIL,
};

int fp_dev_supports_imaging(struct fp_dev *dev);
int fp_dev_img_capture(struct fp_dev *dev, int unconditional,
	struct fp_img **img);
int fp_dev_get_img_width(struct fp_dev *dev);
int fp_dev_get_img_height(struct fp_dev *dev);

/**
 * fp_enroll_result:
 * @FP_ENROLL_COMPLETE: Enrollment completed successfully, the enrollment data has been
 * returned to the caller.
 * @FP_ENROLL_FAIL: Enrollment failed due to incomprehensible data; this may occur when
 * the user scans a different finger on each enroll stage.
 * @FP_ENROLL_PASS: Enroll stage passed; more stages are need to complete the process.
 * @FP_ENROLL_RETRY: The enrollment scan did not succeed due to poor scan quality or
 * other general user scanning problem.
 * @FP_ENROLL_RETRY_TOO_SHORT: The enrollment scan did not succeed because the finger swipe was
 * too short.
 * @FP_ENROLL_RETRY_CENTER_FINGER: The enrollment scan did not succeed because the finger was not
 * centered on the scanner.
 * @FP_ENROLL_RETRY_REMOVE_FINGER: The verification scan did not succeed due to quality or pressure
 * problems; the user should remove their finger from the scanner before
 * retrying.
 *
 *
 * Enrollment result codes returned from fp_enroll_finger().
 * Result codes with RETRY in the name suggest that the scan failed due to
 * user error. Applications will generally want to inform the user of the
 * problem and then retry the enrollment stage. For more info on the semantics
 * of interpreting these result codes and tracking enrollment process, see
 * [Enrolling](libfprint-Devices-operations.html#enrolling)
 */
enum fp_enroll_result {
	FP_ENROLL_COMPLETE = 1,
	FP_ENROLL_FAIL,
	FP_ENROLL_PASS,
	FP_ENROLL_RETRY = 100,
	FP_ENROLL_RETRY_TOO_SHORT,
	FP_ENROLL_RETRY_CENTER_FINGER,
	FP_ENROLL_RETRY_REMOVE_FINGER,
};

int fp_enroll_finger_img(struct fp_dev *dev, struct fp_print_data **print_data,
	struct fp_img **img);

/**
 * fp_enroll_finger:
 * @dev: the device
 * @print_data: a location to return the resultant enrollment data from
 * the final stage. Must be freed with fp_print_data_free() after use.
 *
 * Performs an enroll stage. See [Enrolling](libfprint-Devices-operations.html#enrolling)
 * for an explanation of enroll stages. This function is just a shortcut to
 * calling fp_enroll_finger_img() with a %NULL image parameter. Be sure to read
 * the description of fp_enroll_finger_img() in order to understand its behaviour.
 *
 * Returns: negative code on error, otherwise a code from #fp_enroll_result
 */
static inline int fp_enroll_finger(struct fp_dev *dev,
	struct fp_print_data **print_data)
{
	return fp_enroll_finger_img(dev, print_data, NULL);
}

/**
 * fp_verify_result:
 * @FP_VERIFY_NO_MATCH: The scan completed successfully, but the newly scanned fingerprint
 * does not match the fingerprint being verified against.
 * In the case of identification, this return code indicates that the
 * scanned finger could not be found in the print gallery.
 * @FP_VERIFY_MATCH: The scan completed successfully and the newly scanned fingerprint does
 * match the fingerprint being verified, or in the case of identification,
 * the scanned fingerprint was found in the print gallery.
 * @FP_VERIFY_RETRY: The scan did not succeed due to poor scan quality or other general
 * user scanning problem.
 * @FP_VERIFY_RETRY_TOO_SHORT: The scan did not succeed because the finger swipe was too short.
 * @FP_VERIFY_RETRY_CENTER_FINGER: The scan did not succeed because the finger was not centered on the
 * scanner.
 * @FP_VERIFY_RETRY_REMOVE_FINGER: The scan did not succeed due to quality or pressure problems; the user
 * should remove their finger from the scanner before retrying.
 *
 * Verification result codes returned from fp_verify_finger(). Return codes
 * are also shared with fp_identify_finger().
 * Result codes with RETRY in the name suggest that the scan failed due to
 * user error. Applications will generally want to inform the user of the
 * problem and then retry the verify operation.
 */
enum fp_verify_result {
	FP_VERIFY_NO_MATCH = 0,
	FP_VERIFY_MATCH = 1,
	FP_VERIFY_RETRY = FP_ENROLL_RETRY,
	FP_VERIFY_RETRY_TOO_SHORT = FP_ENROLL_RETRY_TOO_SHORT,
	FP_VERIFY_RETRY_CENTER_FINGER = FP_ENROLL_RETRY_CENTER_FINGER,
	FP_VERIFY_RETRY_REMOVE_FINGER = FP_ENROLL_RETRY_REMOVE_FINGER,
};

int fp_verify_finger_img(struct fp_dev *dev,
	struct fp_print_data *enrolled_print, struct fp_img **img);

/**
 * fp_verify_finger:
 * @dev: the device to perform the scan.
 * @enrolled_print: the print to verify against. Must have been previously
 * enrolled with a device compatible to the device selected to perform the scan.
 *
 * Performs a new scan and verify it against a previously enrolled print. This
 * function is just a shortcut to calling fp_verify_finger_img() with a NULL
 * image output parameter.
 *
 * Returns: negative code on error, otherwise a code from #fp_verify_result
 * \sa fp_verify_finger_img()
 */
static inline int fp_verify_finger(struct fp_dev *dev,
	struct fp_print_data *enrolled_print)
{
	return fp_verify_finger_img(dev, enrolled_print, NULL);
}

int fp_dev_supports_identification(struct fp_dev *dev);
int fp_identify_finger_img(struct fp_dev *dev,
	struct fp_print_data **print_gallery, size_t *match_offset,
	struct fp_img **img);

/**
 * fp_identify_finger:
 * @dev: the device to perform the scan.
 * @print_gallery: %NULL-terminated array of pointers to the prints to
 * identify against. Each one must have been previously enrolled with a device
 * compatible to the device selected to perform the scan.
 * @match_offset: output location to store the array index of the matched
 * gallery print (if any was found). Only valid if FP_VERIFY_MATCH was
 * returned.

 * Performs a new scan and attempts to identify the scanned finger against a
 * collection of previously enrolled fingerprints. This function is just a
 * shortcut to calling fp_identify_finger_img() with a %NULL image output
 * parameter.
 *
 * Returns: negative code on error, otherwise a code from #fp_verify_result
 * \sa fp_identify_finger_img()
 */
static inline int fp_identify_finger(struct fp_dev *dev,
	struct fp_print_data **print_gallery, size_t *match_offset)
{
	return fp_identify_finger_img(dev, print_gallery, match_offset, NULL);
}

/* Data handling */
int fp_print_data_load(struct fp_dev *dev, enum fp_finger finger,
	struct fp_print_data **data);
int fp_print_data_from_dscv_print(struct fp_dscv_print *print,
	struct fp_print_data **data);
int fp_print_data_save(struct fp_print_data *data, enum fp_finger finger);
int fp_print_data_delete(struct fp_dev *dev, enum fp_finger finger);
void fp_print_data_free(struct fp_print_data *data);
size_t fp_print_data_get_data(struct fp_print_data *data, unsigned char **ret);
struct fp_print_data *fp_print_data_from_data(unsigned char *buf,
	size_t buflen);
uint16_t fp_print_data_get_driver_id(struct fp_print_data *data);
uint32_t fp_print_data_get_devtype(struct fp_print_data *data);

/* Image handling */

/**
 * fp_minutia:
 *
 * FIXME
 */
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

int fp_img_get_height(struct fp_img *img);
int fp_img_get_width(struct fp_img *img);
unsigned char *fp_img_get_data(struct fp_img *img);
int fp_img_save_to_file(struct fp_img *img, char *path);
void fp_img_standardize(struct fp_img *img);
struct fp_img *fp_img_binarize(struct fp_img *img);
struct fp_minutia **fp_img_get_minutiae(struct fp_img *img, int *nr_minutiae);
void fp_img_free(struct fp_img *img);

/* Polling and timing */

/**
 * fp_pollfd:
 *
 */
struct fp_pollfd {
	int fd;
	short events;
};

int fp_handle_events_timeout(struct timeval *timeout);
int fp_handle_events(void);
size_t fp_get_pollfds(struct fp_pollfd **pollfds);
int fp_get_next_timeout(struct timeval *tv);

typedef void (*fp_pollfd_added_cb)(int fd, short events);
typedef void (*fp_pollfd_removed_cb)(int fd);
void fp_set_pollfd_notifiers(fp_pollfd_added_cb added_cb,
	fp_pollfd_removed_cb removed_cb);

/* Library */
int fp_init(void);
void fp_exit(void);
void fp_set_debug(int level);

/* Asynchronous I/O */

typedef void (*fp_dev_open_cb)(struct fp_dev *dev, int status, void *user_data);
int fp_async_dev_open(struct fp_dscv_dev *ddev, fp_dev_open_cb callback,
	void *user_data);

typedef void (*fp_dev_close_cb)(struct fp_dev *dev, void *user_data);
void fp_async_dev_close(struct fp_dev *dev, fp_dev_close_cb callback,
	void *user_data);

typedef void (*fp_enroll_stage_cb)(struct fp_dev *dev, int result,
	struct fp_print_data *print, struct fp_img *img, void *user_data);
int fp_async_enroll_start(struct fp_dev *dev, fp_enroll_stage_cb callback,
	void *user_data);

typedef void (*fp_enroll_stop_cb)(struct fp_dev *dev, void *user_data);
int fp_async_enroll_stop(struct fp_dev *dev, fp_enroll_stop_cb callback,
	void *user_data);

typedef void (*fp_verify_cb)(struct fp_dev *dev, int result,
	struct fp_img *img, void *user_data);
int fp_async_verify_start(struct fp_dev *dev, struct fp_print_data *data,
	fp_verify_cb callback, void *user_data);

typedef void (*fp_verify_stop_cb)(struct fp_dev *dev, void *user_data);
int fp_async_verify_stop(struct fp_dev *dev, fp_verify_stop_cb callback,
	void *user_data);

typedef void (*fp_identify_cb)(struct fp_dev *dev, int result,
	size_t match_offset, struct fp_img *img, void *user_data);
int fp_async_identify_start(struct fp_dev *dev, struct fp_print_data **gallery,
	fp_identify_cb callback, void *user_data);

typedef void (*fp_identify_stop_cb)(struct fp_dev *dev, void *user_data);
int fp_async_identify_stop(struct fp_dev *dev, fp_identify_stop_cb callback,
	void *user_data);

typedef void (*fp_capture_cb)(struct fp_dev *dev, int result,
	struct fp_img *img, void *user_data);
int fp_async_capture_start(struct fp_dev *dev, int unconditional, fp_capture_cb callback, void *user_data);

typedef void (*fp_capture_stop_cb)(struct fp_dev *dev, void *user_data);
int fp_async_capture_stop(struct fp_dev *dev, fp_capture_stop_cb callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif

