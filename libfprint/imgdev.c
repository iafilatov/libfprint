/*
 * Core imaging device functions for libfprint
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

#include <errno.h>
#include <glib.h>

#include "fp_internal.h"

static int img_dev_init(struct fp_dev *dev, unsigned long driver_data)
{
	struct fp_img_dev *imgdev = g_malloc0(sizeof(*imgdev));
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(dev->drv);
	int r = 0;

	imgdev->dev = dev;
	dev->priv = imgdev;
	dev->nr_enroll_stages = 1;

	/* for consistency in driver code, allow udev access through imgdev */
	imgdev->udev = dev->udev;

	if (imgdrv->init) {
		r = imgdrv->init(imgdev, driver_data);
		if (r)
			goto err;
	}

	return 0;
err:
	g_free(imgdev);
	return r;
}

static void img_dev_exit(struct fp_dev *dev)
{
	struct fp_img_dev *imgdev = dev->priv;
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(dev->drv);

	if (imgdrv->exit)
		imgdrv->exit(imgdev);

	g_free(imgdev);
}

int fpi_imgdev_get_img_width(struct fp_img_dev *imgdev)
{
	struct fp_driver *drv = imgdev->dev->drv;
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(drv);
	return imgdrv->img_width;
}

int fpi_imgdev_get_img_height(struct fp_img_dev *imgdev)
{
	struct fp_driver *drv = imgdev->dev->drv;
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(drv);
	return imgdrv->img_height;
}

int fpi_imgdev_capture(struct fp_img_dev *imgdev, int unconditional,
	struct fp_img **_img)
{
	struct fp_driver *drv = imgdev->dev->drv;
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(drv);
	struct fp_img *img;
	int r;

	if (!_img) {
		fp_err("no image pointer given");
		return -EINVAL;
	}

	if (!imgdrv->capture) {
		fp_err("img driver %s has no capture func", drv->name);
		return -ENOTSUP;
	}

	if (unconditional) {
		if (!(imgdrv->flags & FP_IMGDRV_SUPPORTS_UNCONDITIONAL_CAPTURE)) {
			fp_dbg("requested unconditional capture, but driver %s does not "
				"support it", drv->name);
			return -ENOTSUP;
		}
	}

	fp_dbg("%s will handle capture request", drv->name);

	if (!unconditional && imgdrv->await_finger_on) {
		r = imgdrv->await_finger_on(imgdev);
		if (r) {
			fp_err("await_finger_on failed with error %d", r);
			return r;
		}
	}

	r = imgdrv->capture(imgdev, unconditional, &img);
	if (r) {
		fp_err("capture failed with error %d", r);
		return r;
	}

	if (img == NULL) {
		fp_err("capture succeeded but no image returned?");
		return -ENODATA;
	}

	if (!unconditional && imgdrv->await_finger_off) {
		r = imgdrv->await_finger_off(imgdev);
		if (r) {
			fp_err("await_finger_off failed with error %d", r);
			fp_img_free(img);
			return r;
		}
	}

	if (imgdrv->img_width > 0) {
		img->width = imgdrv->img_width;
	} else if (img->width <= 0) {
		fp_err("no image width assigned");
		goto err;
	}

	if (imgdrv->img_height > 0) {
		img->height = imgdrv->img_height;
	} else if (img->height <= 0) {
		fp_err("no image height assigned");
		goto err;
	}

	if (!fpi_img_is_sane(img)) {
		fp_err("image is not sane!");
		goto err;
	}

	*_img = img;
	return 0;
err:
	fp_img_free(img);
	return -EIO;
}

#define MIN_ACCEPTABLE_MINUTIAE 10

int img_dev_enroll(struct fp_dev *dev, gboolean initial, int stage,
	struct fp_print_data **ret, struct fp_img **_img)
{
	struct fp_img *img;
	struct fp_img_dev *imgdev = dev->priv;
	struct fp_print_data *print;
	int r;

	/* FIXME: convert to 3-stage enroll mechanism, where we scan 3 prints,
	 * use NFIQ to pick the best one, and discard the others */

	r = fpi_imgdev_capture(imgdev, 0, &img);

	/* If we got an image, standardize it and return it even if the scan
	 * quality was too low for processing. */
	if (img)
		fp_img_standardize(img);
	if (_img)
		*_img = img;
	if (r)
		return r;

	r = fpi_img_detect_minutiae(imgdev, img, &print);
	if (r < 0)
		return r;
	if (r < MIN_ACCEPTABLE_MINUTIAE) {
		fp_dbg("not enough minutiae, %d/%d", r, MIN_ACCEPTABLE_MINUTIAE);
		fp_print_data_free(print);
		return FP_ENROLL_RETRY;
	}

	*ret = print;
	return FP_ENROLL_COMPLETE;
}

#define BOZORTH3_DEFAULT_THRESHOLD 40

static int img_dev_verify(struct fp_dev *dev,
	struct fp_print_data *enrolled_print, struct fp_img **_img)
{
	struct fp_img_dev *imgdev = dev->priv;
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(dev->drv);
	struct fp_img *img = NULL;
	struct fp_print_data *print;
	int match_score = imgdrv->bz3_threshold;
	int r;

	r = fpi_imgdev_capture(imgdev, 0, &img);

	/* If we got an image, standardize it and return it even if the scan
	 * quality was too low for processing. */
	if (img)
		fp_img_standardize(img);
	if (_img)
		*_img = img;
	if (r)
		return r;

	r = fpi_img_detect_minutiae(imgdev, img, &print);
	if (r < 0)
		return r;
	if (r < MIN_ACCEPTABLE_MINUTIAE) {
		fp_dbg("not enough minutiae, %d/%d", r, MIN_ACCEPTABLE_MINUTIAE);
		fp_print_data_free(print);
		return FP_VERIFY_RETRY;
	}

	if (match_score == 0)
		match_score = BOZORTH3_DEFAULT_THRESHOLD;

	r = fpi_img_compare_print_data(enrolled_print, print);
	fp_print_data_free(print);
	if (r < 0)
		return r;
	if (r >= match_score)
		return FP_VERIFY_MATCH;
	else
		return FP_VERIFY_NO_MATCH;
}

void fpi_img_driver_setup(struct fp_img_driver *idriver)
{
	idriver->driver.type = DRIVER_IMAGING;
	idriver->driver.init = img_dev_init;
	idriver->driver.exit = img_dev_exit;
	idriver->driver.enroll = img_dev_enroll;
	idriver->driver.verify = img_dev_verify;
}

