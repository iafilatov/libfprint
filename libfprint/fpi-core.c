/*
 * Core functions for libfprint
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

#include <config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <libusb.h>

#include "fp_internal.h"

libusb_context *fpi_usb_ctx = NULL;
GSList *opened_devices = NULL;

/**
 * SECTION:discovery
 * @title: Device discovery
 * @short_description: Device discovery functions
 *
 * These functions allow you to scan the system for supported fingerprint
 * scanning hardware. This is your starting point when integrating libfprint
 * into your software.
 *
 * When you've identified a discovered device that you would like to control,
 * you can open it with fp_dev_open(). Note that discovered devices may no
 * longer be available at the time when you want to open them, for example
 * the user may have unplugged the device.
 */

/**
 * SECTION:drv
 * @title: Driver operations
 * @short_description: Driver operation functions
 *
 * Internally, libfprint is abstracted into various drivers to communicate
 * with the different types of supported fingerprint readers. libfprint works
 * hard so that you don't have to care about these internal abstractions,
 * however there are some situations where you may be interested in a little
 * behind-the-scenes driver info.
 *
 * You can obtain the driver for a device using fp_dev_get_driver(), which
 * you can pass to the functions documented on this page.
 */

/**
 * SECTION:dev
 * @title: Devices operations
 * @short_description: Device operation functions
 *
 * In order to interact with fingerprint scanners, your software will
 * interface primarily with libfprint's representation of devices, detailed
 * on this page.
 *
 * # Enrolling # {#enrolling}
 *
 * Enrolling is represented within libfprint as a multi-stage process. This
 * slightly complicates things for application developers, but is required
 * for a smooth process.
 *
 * Some devices require the user to scan their finger multiple times in
 * order to complete the enrollment process. libfprint must return control
 * to your application in-between each scan in order for your application to
 * instruct the user to swipe their finger again. Each scan is referred to
 * as a stage, so a device that requires 3 scans for enrollment corresponds
 * to you running 3 enrollment stages using libfprint.
 *
 * The fp_dev_get_nr_enroll_stages() function can be used to find out how
 * many enroll stages are needed.
 *
 * In order to complete an enroll stage, you call an enroll function such
 * as fp_enroll_finger(). The return of this function does not necessarily
 * indicate that a stage has completed though, as the user may not have
 * produced a good enough scan. Each stage may have to be retried several
 * times.
 *
 * The exact semantics of the enroll functions are described in the
 * fp_enroll_finger() documentation. You should pay careful attention to the
 * details.
 *
 * # Imaging # {#imaging}
 *
 * libfprint provides you with some ways to retrieve images of scanned
 * fingers, such as the fp_dev_img_capture() function, or some enroll/verify
 * function variants which provide images. You may wish to do something with
 * such images in your application.
 *
 * However, you must be aware that not all hardware supported by libfprint
 * operates like this. Most hardware does operate simply by sending
 * fingerprint images to the host computer for further processing, but some
 * devices do all fingerprint processing in hardware and do not present images
 * to the host computer.
 *
 * You can use fp_dev_supports_imaging() to see if image capture is possible
 * on a particular device. Your application must be able to cope with the
 * fact that libfprint does support regular operations (e.g. enrolling and
 * verification) on some devices which do not provide images.
 */

/**
 * SECTION:fpi-core
 * @title: Driver structures
 * @short_description: Driver structures
 *
 * Driver structures need to be defined inside each driver in
 * order for the core library to know what function to call, and the capabilities
 * of the driver and the devices it supports.
 */

/**
 * SECTION:fpi-core-img
 * @title: Image driver structures
 * @short_description: Image driver structures
 *
 * Image driver structures need to be defined inside each image driver in
 * order for the core library to know what function to call, and the capabilities
 * of the driver and the devices it supports. Its structure is based off the
 * #fp_driver struct.
 */

static GSList *registered_drivers = NULL;

static void register_driver(struct fp_driver *drv)
{
	if (drv->id == 0) {
		fp_err("not registering driver %s: driver ID is 0", drv->name);
		return;
	}
	registered_drivers = g_slist_prepend(registered_drivers, (gpointer) drv);
	fp_dbg("registered driver %s", drv->name);
}

#include "drivers_arrays.h"

static void register_drivers(void)
{
	unsigned int i;

	for (i = 0; i < G_N_ELEMENTS(primitive_drivers); i++)
		register_driver(primitive_drivers[i]);

	for (i = 0; i < G_N_ELEMENTS(img_drivers); i++) {
		struct fp_img_driver *imgdriver = img_drivers[i];
		fpi_img_driver_setup(imgdriver);
		register_driver(&imgdriver->driver);
	}
}

API_EXPORTED struct fp_driver **fprint_get_drivers (void)
{
	GPtrArray *array;
	unsigned int i;

	array = g_ptr_array_new ();
	for (i = 0; i < G_N_ELEMENTS(primitive_drivers); i++)
		g_ptr_array_add (array, primitive_drivers[i]);

	for (i = 0; i < G_N_ELEMENTS(img_drivers); i++)
		g_ptr_array_add (array, &(img_drivers[i]->driver));

	/* Add a null item terminating the array */
	g_ptr_array_add (array, NULL);

	return (struct fp_driver **) g_ptr_array_free (array, FALSE);
}

static struct fp_driver *find_supporting_driver(libusb_device *udev,
	const struct usb_id **usb_id, uint32_t *devtype)
{
	int ret;
	GSList *elem = registered_drivers;
	struct libusb_device_descriptor dsc;

	const struct usb_id *best_usb_id;
	struct fp_driver *best_drv;
	uint32_t best_devtype;
	int drv_score = 0;

	ret = libusb_get_device_descriptor(udev, &dsc);
	if (ret < 0) {
		fp_err("Failed to get device descriptor");
		return NULL;
	}

	best_drv = NULL;
	best_devtype = 0;

	do {
		struct fp_driver *drv = elem->data;
		uint32_t type = 0;
		const struct usb_id *id;

		for (id = drv->id_table; id->vendor; id++) {
			if (dsc.idVendor == id->vendor && dsc.idProduct == id->product) {
				if (drv->discover) {
					int r = drv->discover(&dsc, &type);
					if (r < 0)
						fp_err("%s discover failed, code %d", drv->name, r);
					if (r <= 0)
						continue;
					/* Has a discover function, and matched our device */
					drv_score = 100;
				} else {
					/* Already got a driver as good */
					if (drv_score >= 50)
						continue;
					drv_score = 50;
				}
				fp_dbg("driver %s supports USB device %04x:%04x",
					drv->name, id->vendor, id->product);
				best_usb_id = id;
				best_drv = drv;
				best_devtype = type;

				/* We found the best possible driver */
				if (drv_score == 100)
					break;
			}
		}
	} while ((elem = g_slist_next(elem)));

	if (best_drv != NULL) {
		fp_dbg("selected driver %s supports USB device %04x:%04x",
		       best_drv->name, dsc.idVendor, dsc.idProduct);
		*devtype = best_devtype;
		*usb_id = best_usb_id;
	}

	return best_drv;
}

static struct fp_dscv_dev *discover_dev(libusb_device *udev)
{
	const struct usb_id *usb_id;
	struct fp_driver *drv;
	struct fp_dscv_dev *ddev;
	uint32_t devtype;

	drv = find_supporting_driver(udev, &usb_id, &devtype);

	if (!drv)
		return NULL;

	ddev = g_malloc0(sizeof(*ddev));
	ddev->drv = drv;
	ddev->udev = udev;
	ddev->driver_data = usb_id->driver_data;
	ddev->devtype = devtype;
	return ddev;
}

/**
 * fp_discover_devs:
 *
 * Scans the system and returns a list of discovered devices. This is your
 * entry point into finding a fingerprint reader to operate.
 *
 * Returns: a nul-terminated list of discovered devices. Must be freed with
 * fp_dscv_devs_free() after use.
 */
API_EXPORTED struct fp_dscv_dev **fp_discover_devs(void)
{
	GPtrArray *tmparray;
	libusb_device *udev;
	libusb_device **devs;
	int r;
	int i = 0;

	g_return_val_if_fail (registered_drivers != NULL, NULL);

	r = libusb_get_device_list(fpi_usb_ctx, &devs);
	if (r < 0) {
		fp_err("couldn't enumerate USB devices, error %d", r);
		return NULL;
	}

	tmparray = g_ptr_array_new ();

	/* Check each device against each driver, temporarily storing successfully
	 * discovered devices in a GPtrArray. */
	while ((udev = devs[i++]) != NULL) {
		struct fp_dscv_dev *ddev = discover_dev(udev);
		if (!ddev)
			continue;
		/* discover_dev() doesn't hold a reference to the udev,
		 * so increase the reference for ddev to hold this ref */
		libusb_ref_device(udev);
		g_ptr_array_add (tmparray, (gpointer) ddev);
	}
	libusb_free_device_list(devs, 1);

	/* Convert our temporary array into a standard NULL-terminated pointer
	 * array. */
	g_ptr_array_add (tmparray, NULL);
	return (struct fp_dscv_dev **) g_ptr_array_free (tmparray, FALSE);
}

/**
 * fp_dscv_devs_free:
 * @devs: the list of discovered devices. If %NULL, function simply
 * returns.
 *
 * Free a list of discovered devices. This function destroys the list and all
 * discovered devices that it included, so make sure you have opened your
 * discovered device <emphasis role="strong">before</emphasis> freeing the list.
 */
API_EXPORTED void fp_dscv_devs_free(struct fp_dscv_dev **devs)
{
	int i;
	if (!devs)
		return;

	for (i = 0; devs[i]; i++) {
		libusb_unref_device(devs[i]->udev);
		g_free(devs[i]);
	}
	g_free(devs);
}

/**
 * fp_dscv_dev_get_driver:
 * @dev: the discovered device
 *
 * Gets the #fp_driver for a discovered device.
 *
 * Returns: the driver backing the device
 */
API_EXPORTED struct fp_driver *fp_dscv_dev_get_driver(struct fp_dscv_dev *dev)
{
	return dev->drv;
}

/**
 * fp_dscv_dev_get_driver_id:
 * @dev: a discovered fingerprint device
 *
 * Returns a unique driver identifier for the underlying driver
 * for that device.
 *
 * Returns: the ID for #dev
 */
API_EXPORTED uint16_t fp_dscv_dev_get_driver_id(struct fp_dscv_dev *dev)
{
	return fp_driver_get_driver_id(fp_dscv_dev_get_driver(dev));
}

/**
 * fp_dscv_dev_get_devtype:
 * @dev: the discovered device
 *
 * Gets the [devtype](advanced-topics.html#device-types) for a discovered device.
 *
 * Returns: the devtype of the device
 */
API_EXPORTED uint32_t fp_dscv_dev_get_devtype(struct fp_dscv_dev *dev)
{
	return dev->devtype;
}

enum fp_print_data_type fpi_driver_get_data_type(struct fp_driver *drv)
{
	switch (drv->type) {
	case DRIVER_PRIMITIVE:
		return PRINT_DATA_RAW;
	case DRIVER_IMAGING:
		return PRINT_DATA_NBIS_MINUTIAE;
	default:
		fp_err("unrecognised drv type %d", drv->type);
		return PRINT_DATA_RAW;
	}
}

/**
 * fp_dscv_dev_supports_print_data:
 * @dev: the discovered device
 * @print: the print for compatibility checking
 *
 * Determines if a specific #fp_print_data stored print appears to be
 * compatible with a discovered device.
 *
 * Returns: 1 if the print is compatible with the device, 0 otherwise
 */
API_EXPORTED int fp_dscv_dev_supports_print_data(struct fp_dscv_dev *dev,
	struct fp_print_data *print)
{
	return fpi_print_data_compatible(dev->drv->id, dev->devtype,
		fpi_driver_get_data_type(dev->drv), print->driver_id, print->devtype,
		print->type);
}

/**
 * fp_dscv_dev_supports_dscv_print:
 * @dev: the discovered device
 * @print: the discovered print for compatibility checking
 *
 * Determines if a specific #fp_dscv_print discovered print appears to be
 * compatible with a discovered device.
 *
 * Returns: 1 if the print is compatible with the device, 0 otherwise
 *
 * Deprecated: Do not use.
 */
API_EXPORTED int fp_dscv_dev_supports_dscv_print(struct fp_dscv_dev *dev,
	struct fp_dscv_print *print)
{
	return fpi_print_data_compatible(dev->drv->id, dev->devtype, 0,
		print->driver_id, print->devtype, 0);
}

/**
 * fp_dscv_dev_for_print_data:
 * @devs: a list of discovered devices
 * @print: the print under inspection
 *
 * Searches a list of discovered devices for a device that appears to be
 * compatible with a #fp_print_data stored print.
 *
 * Returns: the first discovered device that appears to support the print, or
 * %NULL if no apparently compatible devices could be found
 *
 * Deprecated: Do not use.
 */
API_EXPORTED struct fp_dscv_dev *fp_dscv_dev_for_print_data(struct fp_dscv_dev **devs,
	struct fp_print_data *print)
{
	struct fp_dscv_dev *ddev;
	int i;

	for (i = 0; (ddev = devs[i]); i++)
		if (fp_dscv_dev_supports_print_data(ddev, print))
			return ddev;
	return NULL;
}

/**
 * fp_dscv_dev_for_dscv_print:
 * @devs: a list of discovered devices
 * @print: the print under inspection
 *
 * Searches a list of discovered devices for a device that appears to be
 * compatible with a #fp_dscv_print discovered print.
 *
 * Returns: the first discovered device that appears to support the print, or
 * %NULL if no apparently compatible devices could be found
 *
 * Deprecated: Do not use.
 */
API_EXPORTED struct fp_dscv_dev *fp_dscv_dev_for_dscv_print(struct fp_dscv_dev **devs,
	struct fp_dscv_print *print)
{
	struct fp_dscv_dev *ddev;
	int i;

	for (i = 0; (ddev = devs[i]); i++) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
		if (fp_dscv_dev_supports_dscv_print(ddev, print))
			return ddev;
#pragma GCC diagnostic pop
	}
	return NULL;
}

/**
 * fp_dev_get_driver:
 * @dev: the struct #fp_dev device
 *
 * Get the #fp_driver for a fingerprint device.
 *
 * Returns: the driver controlling the device
 */
API_EXPORTED struct fp_driver *fp_dev_get_driver(struct fp_dev *dev)
{
	return dev->drv;
}

/**
 * fp_dev_get_nr_enroll_stages:
 * @dev: the struct #fp_dev device
 *
 * Gets the number of [enroll stages](intro.html#enrollment) required to enroll a
 * fingerprint with the device.
 *
 * Returns: the number of enroll stages
 */
API_EXPORTED int fp_dev_get_nr_enroll_stages(struct fp_dev *dev)
{
	return dev->nr_enroll_stages;
}

/**
 * fp_dev_get_devtype:
 * @dev: the struct #fp_dev device
 *
 * Gets the [devtype](advanced-topics.html#device-types) for a device.
 *
 * Returns: the devtype
 */
API_EXPORTED uint32_t fp_dev_get_devtype(struct fp_dev *dev)
{
	return dev->devtype;
}

/**
 * fp_dev_supports_print_data:
 * @dev: the struct #fp_dev device
 * @data: the stored print
 *
 * Determines if a stored print is compatible with a certain device.
 *
 * Returns: 1 if the print is compatible with the device, 0 if not
 */
API_EXPORTED int fp_dev_supports_print_data(struct fp_dev *dev,
	struct fp_print_data *data)
{
	return fpi_print_data_compatible(dev->drv->id, dev->devtype,
		fpi_driver_get_data_type(dev->drv), data->driver_id, data->devtype,
		data->type);
}

/**
 * fp_dev_supports_dscv_print:
 * @dev: the struct #fp_dev device
 * @print: the discovered print
 *
 * Determines if a #fp_dscv_print discovered print appears to be compatible
 * with a certain device.
 *
 * Returns: 1 if the print is compatible with the device, 0 if not
 *
 * Deprecated: Do not use.
 */
API_EXPORTED int fp_dev_supports_dscv_print(struct fp_dev *dev,
	struct fp_dscv_print *print)
{
	return fpi_print_data_compatible(dev->drv->id, dev->devtype,
		0, print->driver_id, print->devtype, 0);
}

/**
 * fp_driver_get_name:
 * @drv: the driver
 *
 * Retrieves the name of the driver. For example: "upekts"
 *
 * Returns: the driver name. Must not be modified or freed.
 */
API_EXPORTED const char *fp_driver_get_name(struct fp_driver *drv)
{
	return drv->name;
}

/**
 * fp_driver_get_full_name:
 * @drv: the driver
 *
 * Retrieves a descriptive name of the driver. For example: "UPEK TouchStrip"
 *
 * Returns: the descriptive name. Must not be modified or freed.
 */
API_EXPORTED const char *fp_driver_get_full_name(struct fp_driver *drv)
{
	return drv->full_name;
}

/**
 * fp_driver_get_driver_id:
 * @drv: the driver
 *
 * Retrieves the driver ID code for a driver.
 *
 * Returns: the driver ID
 */
API_EXPORTED uint16_t fp_driver_get_driver_id(struct fp_driver *drv)
{
	return drv->id;
}

/**
 * fp_driver_get_scan_type:
 * @drv: the driver
 *
 * Retrieves the scan type for the devices associated with the driver.
 *
 * Returns: the scan type
 */
API_EXPORTED enum fp_scan_type fp_driver_get_scan_type(struct fp_driver *drv)
{
	return drv->scan_type;
}

/**
 * fp_driver_supports_imaging:
 * @drv: the driver
 *
 * Determines if a driver has imaging capabilities. If a driver has imaging
 * capabilities you are able to perform imaging operations such as retrieving
 * scan images using fp_dev_img_capture(). However, not all drivers support
 * imaging devices – some do all processing in hardware. This function will
 * indicate which class a device in question falls into.
 *
 * Returns: 1 if the device is an imaging device, 0 if the device does not
 * provide images to the host computer
 */
API_EXPORTED int fp_driver_supports_imaging(struct fp_driver *drv)
{
	return drv->capture_start != NULL;
}

/**
 * fp_dev_supports_imaging:
 * @dev: the struct #fp_dev device
 *
 * Determines if a device has imaging capabilities. If a device has imaging
 * capabilities you are able to perform imaging operations such as retrieving
 * scan images using fp_dev_img_capture(). However, not all devices are
 * imaging devices – some do all processing in hardware. This function will
 * indicate which class a device in question falls into.
 *
 * Returns: 1 if the device is an imaging device, 0 if the device does not
 * provide images to the host computer
 */
API_EXPORTED int fp_dev_supports_imaging(struct fp_dev *dev)
{
	return dev->drv->capture_start != NULL;
}

/**
 * fp_dev_supports_identification:
 * @dev: the struct #fp_dev device
 *
 * Determines if a device is capable of [identification](intro.html#identification)
 * through fp_identify_finger() and similar. Not all devices support this
 * functionality.
 *
 * Returns: 1 if the device is capable of identification, 0 otherwise.
 */
API_EXPORTED int fp_dev_supports_identification(struct fp_dev *dev)
{
	return dev->drv->identify_start != NULL;
}

/**
 * fp_dev_get_img_width:
 * @dev: the struct #fp_dev device
 *
 * Gets the expected width of images that will be captured from the device.
 * This function will return -1 for devices that are not
 * [imaging devices](libfprint-Devices-operations.html#imaging). If the width of images from this device
 * can vary, 0 will be returned.
 *
 * Returns: the expected image width, or 0 for variable, or -1 for non-imaging
 * devices.
 */
API_EXPORTED int fp_dev_get_img_width(struct fp_dev *dev)
{
	if (!dev->img_dev) {
		fp_dbg("get image width for non-imaging device");
		return -1;
	}

	return fpi_imgdev_get_img_width(dev->img_dev);
}

/**
 * fp_dev_get_img_height:
 * @dev: the struct #fp_dev device
 *
 * Gets the expected height of images that will be captured from the device.
 * This function will return -1 for devices that are not
 * [imaging devices](libfprint-Devices-operations.html#imaging). If the height of images from this device
 * can vary, 0 will be returned.
 *
 * Returns: the expected image height, or 0 for variable, or -1 for non-imaging
 * devices.
 */
API_EXPORTED int fp_dev_get_img_height(struct fp_dev *dev)
{
	if (!dev->img_dev) {
		fp_dbg("get image height for non-imaging device");
		return -1;
	}

	return fpi_imgdev_get_img_height(dev->img_dev);
}

/**
 * fp_set_debug:
 * @level: the verbosity level
 *
 * This call does nothing, see fp_init() for details.
 */
API_EXPORTED void fp_set_debug(int level)
{
	/* Nothing */
}

/**
 * fp_init:
 *
 * Initialise libfprint. This function must be called before you attempt to
 * use the library in any way.
 *
 * To enable debug output of libfprint specifically, use GLib's `G_MESSAGES_DEBUG`
 * environment variable as explained in [Running and debugging GLib Applications](https://developer.gnome.org/glib/stable/glib-running.html#G_MESSAGES_DEBUG).
 *
 * The log domains used in libfprint are either `libfprint` or `libfprint-FP_COMPONENT`
 * where `FP_COMPONENT` is defined in the source code for each driver, or component
 * of the library. Starting with `all` and trimming down is advised.
 *
 * To enable debugging of libusb, for USB-based fingerprint reader drivers, use
 * libusb's `LIBUSB_DEBUG` environment variable as explained in the
 * [libusb-1.0 API Reference](http://libusb.sourceforge.net/api-1.0/#msglog).
 *
 * Example:
 *
 * ```
 * LIBUSB_DEBUG=4 G_MESSAGES_DEBUG=all my-libfprint-application
 * ```
 *
 * Returns: 0 on success, non-zero on error.
 */
API_EXPORTED int fp_init(void)
{
	int r;
	G_DEBUG_HERE();

	r = libusb_init(&fpi_usb_ctx);
	if (r < 0)
		return r;

	register_drivers();
	fpi_poll_init();
	return 0;
}

/**
 * fp_exit:
 *
 * Deinitialise libfprint. This function should be called during your program
 * exit sequence. You must not use any libfprint functions after calling this
 * function, unless you call fp_init() again.
 */
API_EXPORTED void fp_exit(void)
{
	G_DEBUG_HERE();

	if (opened_devices) {
		GSList *copy = g_slist_copy(opened_devices);
		GSList *elem = copy;
		fp_dbg("naughty app left devices open on exit!");

		do
			fp_dev_close((struct fp_dev *) elem->data);
		while ((elem = g_slist_next(elem)));

		g_slist_free(copy);
		g_slist_free(opened_devices);
		opened_devices = NULL;
	}

	fpi_data_exit();
	fpi_poll_exit();
	g_slist_free(registered_drivers);
	registered_drivers = NULL;
	libusb_exit(fpi_usb_ctx);
}

