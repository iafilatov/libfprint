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
 * @title: Device operations
 * @short_description: Device operation functions
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
 *
 * Returns: a struct #fp_dev or %NULL
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
 * Returns a struct #fp_img_dev associated with @dev, or %NULL on failure.
 *
 * Returns: a struct #fp_img_dev or %NULL
 */
struct fp_img_dev *
FP_IMG_DEV(struct fp_dev *dev)
{
	g_return_val_if_fail (dev, NULL);
	g_return_val_if_fail (dev->drv, NULL);
	g_return_val_if_fail (dev->drv->type == DRIVER_IMAGING, NULL);
	return dev->img_dev;
}

/**
 * fp_dev_set_instance_data:
 * @dev: a struct #fp_dev
 * @instance_data: a pointer to the instance data
 *
 * Set the instance data for a struct #fp_dev. This is usually a structure
 * private to the driver used to keep state and pass it as user_data to
 * asynchronous functions.
 *
 * The core does not do any memory management for this data, so the driver
 * itself will have to create and free its own structure when appropriate.
 */
void
fp_dev_set_instance_data (struct fp_dev *dev,
			  void          *instance_data)
{
	g_return_if_fail (dev);
	g_return_if_fail (instance_data != NULL);
	g_return_if_fail (dev->instance_data == NULL);

	dev->instance_data = instance_data;
}

/**
 * FP_INSTANCE_DATA:
 * @dev: a struct #fp_dev
 *
 * Returns the instance data set using fp_dev_set_instance_data().
 */
void *
FP_INSTANCE_DATA (struct fp_dev *dev)
{
	g_return_val_if_fail (dev, NULL);

	return dev->instance_data;
}

/**
 * fpi_dev_get_usb_dev:
 * @dev: a struct #fp_dev
 *
 * Returns the #libusb_device_handle associated with @dev or %NULL
 * if none are associated.
 *
 * Returns: a #libusb_device_handle pointer or %NULL
 */
libusb_device_handle *
fpi_dev_get_usb_dev(struct fp_dev *dev)
{
	return dev->udev;
}

/**
 * fpi_dev_set_nr_enroll_stages:
 * @dev: a struct #fp_dev
 * @nr_enroll_stages: the number of enroll stages
 *
 * Sets the number of enroll stages that this device uses. This is
 * usually only necessary for primitive devices which have a hard-coded
 * number of enroll stages baked into their protocol.
 */
void
fpi_dev_set_nr_enroll_stages(struct fp_dev *dev,
	int nr_enroll_stages)
{
	dev->nr_enroll_stages = nr_enroll_stages;
}

/**
 * fpi_dev_get_verify_data:
 * @dev: a struct #fp_dev
 *
 * Returns the verify data associated with @dev.
 * This is usually only necessary for primitive devices which need to
 * have access to the raw verify data as it might have been stored on disk.
 *
 * Returns: a struct #fp_print_data pointer or %NULL
 */
struct fp_print_data *
fpi_dev_get_verify_data(struct fp_dev *dev)
{
	return dev->verify_data;
}
