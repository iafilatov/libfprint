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

#ifndef __FPI_DEV_IMG_H__
#define __FPI_DEV_IMG_H__

#include "fpi-dev.h"
#include "fpi-img.h"

/**
 * fp_imgdev_action:
 * @IMG_ACTION_NONE: no action
 * @IMG_ACTION_ENROLL: device action is enrolling
 * @IMG_ACTION_VERIFY: device action is verifying
 * @IMG_ACTION_IDENTIFY: device action is identifying
 * @IMG_ACTION_CAPTURE: device action is capturing
 *
 * The current action being performed by an imaging device. The current
 * action can be gathered inside the driver using fpi_imgdev_get_action().
 */
enum fp_imgdev_action {
	IMG_ACTION_NONE = 0,
	IMG_ACTION_ENROLL,
	IMG_ACTION_VERIFY,
	IMG_ACTION_IDENTIFY,
	IMG_ACTION_CAPTURE,
};

/**
 * fp_imgdev_state:
 * @IMGDEV_STATE_INACTIVE: inactive
 * @IMGDEV_STATE_AWAIT_FINGER_ON: waiting for the finger to be pressed or swiped
 * @IMGDEV_STATE_CAPTURE: capturing an image
 * @IMGDEV_STATE_AWAIT_FINGER_OFF: waiting for the finger to be removed
 *
 * The state of an imaging device while doing a capture. The state is
 * passed through to the driver using the ::activate() or ::change_state() vfuncs.
 */
enum fp_imgdev_state {
	IMGDEV_STATE_INACTIVE,
	IMGDEV_STATE_AWAIT_FINGER_ON,
	IMGDEV_STATE_CAPTURE,
	IMGDEV_STATE_AWAIT_FINGER_OFF,
};

/**
 * fp_imgdev_enroll_state:
 * @IMG_ACQUIRE_STATE_NONE: doing nothing
 * @IMG_ACQUIRE_STATE_ACTIVATING: activating the device
 * @IMG_ACQUIRE_STATE_AWAIT_FINGER_ON: waiting for the finger to be pressed or swiped
 * @IMG_ACQUIRE_STATE_AWAIT_IMAGE: waiting for the image to be captured
 * @IMG_ACQUIRE_STATE_AWAIT_FINGER_OFF: waiting for the finger to be removed
 * @IMG_ACQUIRE_STATE_DONE: enrollment has all the images it needs
 * @IMG_ACQUIRE_STATE_DEACTIVATING: deactivating the device
 *
 * The state of an imaging device while enrolling a fingerprint. Given that enrollment
 * requires multiple captures, a number of those states will be repeated before
 * the state is @IMG_ACQUIRE_STATE_DONE.
 */
enum fp_imgdev_enroll_state {
	IMG_ACQUIRE_STATE_NONE = 0,
	IMG_ACQUIRE_STATE_ACTIVATING,
	IMG_ACQUIRE_STATE_AWAIT_FINGER_ON,
	IMG_ACQUIRE_STATE_AWAIT_IMAGE,
	IMG_ACQUIRE_STATE_AWAIT_FINGER_OFF,
	IMG_ACQUIRE_STATE_DONE,
	IMG_ACQUIRE_STATE_DEACTIVATING,
};

void fpi_imgdev_open_complete(struct fp_img_dev *imgdev, int status);
void fpi_imgdev_close_complete(struct fp_img_dev *imgdev);
void fpi_imgdev_activate_complete(struct fp_img_dev *imgdev, int status);
void fpi_imgdev_deactivate_complete(struct fp_img_dev *imgdev);
void fpi_imgdev_report_finger_status(struct fp_img_dev *imgdev,
	gboolean present);
void fpi_imgdev_image_captured(struct fp_img_dev *imgdev, struct fp_img *img);
void fpi_imgdev_abort_scan(struct fp_img_dev *imgdev, int result);
void fpi_imgdev_session_error(struct fp_img_dev *imgdev, int error);

enum fp_imgdev_enroll_state fpi_imgdev_get_action_state(struct fp_img_dev *imgdev);
enum fp_imgdev_action fpi_imgdev_get_action(struct fp_img_dev *imgdev);
int fpi_imgdev_get_action_result(struct fp_img_dev *imgdev);
void fpi_imgdev_set_action_result(struct fp_img_dev *imgdev, int action_result);

#endif
