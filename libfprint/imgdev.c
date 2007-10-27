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

API_EXPORTED int fp_imgdev_get_img_width(struct fp_img_dev *imgdev)
{
	struct fp_driver *drv = imgdev->dev->drv;
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(drv);
	return imgdrv->img_width;
}

API_EXPORTED int fp_imgdev_get_img_height(struct fp_img_dev *imgdev)
{
	struct fp_driver *drv = imgdev->dev->drv;
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(drv);
	return imgdrv->img_height;
}

API_EXPORTED int fp_imgdev_capture(struct fp_img_dev *imgdev,
	int unconditional, struct fp_img **image)
{
	struct fp_driver *drv = imgdev->dev->drv;
	struct fp_img_driver *imgdrv = fpi_driver_to_img_driver(drv);
	int r;

	if (!image) {
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

	r = imgdrv->capture(imgdev, unconditional, image);
	if (r) {
		fp_err("capture failed with error %d", r);
		return r;
	}

	if (!unconditional && imgdrv->await_finger_off) {
		r = imgdrv->await_finger_off(imgdev);
		if (r) {
			fp_err("await_finger_off failed with error %d", r);
			return r;
		}
	}

	if (r == 0) {
		struct fp_img *img = *image;
		if (img == NULL) {
			fp_err("capture succeeded but no image returned?");
			return -ENODATA;
		}
		img->width = imgdrv->img_width;
		img->height = imgdrv->img_height;
		if (!fpi_img_is_sane(img)) {
			fp_err("image is not sane!");
			return -EIO;
		}
	}

	return r;
}

void fpi_img_driver_setup(struct fp_img_driver *idriver)
{
	idriver->driver.type = DRIVER_IMAGING;
	idriver->driver.init = img_dev_init;
	idriver->driver.exit = img_dev_exit;
}

