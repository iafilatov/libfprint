/*
 * fp_dev types manipulation
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

#include "fp_internal.h"
#include <glib.h>

/**
 * SECTION:fpi-dev
 * @title: Device structures
 *
 * Those macros and functions will help get access to and from struct #fp_dev,
 * and struct #fp_img_dev types, as well as get and set the instance struct
 * data, eg. the structure containing the data specific to each driver.
 */

/**
 * FP_DEV:
 * @dev: a struct #fp_img_dev
 *
 * Returns the struct #fp_dev associated with @dev, or %NULL on failure.
 */
struct fp_dev *
FP_DEV(struct fp_img_dev *dev)
{
	struct fp_img_dev *imgdev;

	g_return_val_if_fail (dev, NULL);
	imgdev = (struct fp_img_dev *) dev;
	return imgdev->parent;
}

/**
 * FP_IMG_DEV:
 * @dev: a struct #fp_dev representing an imaging device.
 *
 * Returns: a struct #fp_img_dev or %NULL on failure.
 */
struct fp_img_dev *
FP_IMG_DEV(struct fp_dev *dev)
{
	g_return_val_if_fail (dev, NULL);
	g_return_val_if_fail (dev->drv, NULL);
	g_return_val_if_fail (dev->drv->type != DRIVER_IMAGING, NULL);
	return dev->img_dev;
}
