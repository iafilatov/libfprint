/*
 * Core functions for libfprint
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

#include <config.h>
#include <errno.h>
#include <stdio.h>

#include <glib.h>
#include <usb.h>

#include "fp_internal.h"

/**
 * \mainpage libfprint API Reference
 * libfprint is an open source library to provide access to fingerprint
 * scanning devices. For more info, see the
 * <a href="http://www.reactivated.net/fprint/Libfprint">libfprint project
 * homepage</a>.
 *
 * This documentation is aimed at application developers who wish to integrate
 * fingerprint-related functionality into their software. libfprint has been
 * designed so that you only have to do this once - by integrating your
 * software with libfprint, you'll be supporting all the fingerprint readers
 * that we have got our hands on. As such, the API is rather general (and
 * therefore hopefully easy to comprehend!), and does it's best to hide the
 * technical details that required to operate the hardware.
 *
 * This documentation is not aimed at developers wishing to develop and
 * contribute fingerprint device drivers to libfprint.
 *
 * Feedback on this API and it's associated documentation is appreciated. Was
 * anything unclear? Does anything seem unreasonably complicated? Is anything
 * missing? Let us know on the
 * <a href="http://www.reactivated.net/fprint/Mailing_list">mailing list</a>.
 *
 * \section enrollment Enrollment
 *
 * Before you dive into the API, it's worth introducing a couple of concepts.
 *
 * The process of enrolling a finger is where you effectively scan your
 * finger for the purposes of teaching the system what your finger looks like.
 * This means that you scan your fingerprint, then the system processes it and
 * stores some data about your fingerprint to refer to later.
 *
 * \section verification Verification
 *
 * Verification is what most people think of when they think about fingerprint
 * scanning. The process of verification is effectively performing a fresh
 * fingerprint scan, and then comparing that scan to a finger that was 
 * previously enrolled.
 *
 * As an example scenario, verification can be used to implement what people
 * would picture as fingerprint login (i.e. fingerprint replaces password).
 * For example:
 *  - I enroll my fingerprint through some software that trusts I am who I say
 *    I am. This is a prerequisite before I can perform fingerprint-based
 *    login for my account.
 *  - Some time later, I want to login to my computer. I enter my username,
 *    but instead of prompting me for a password, it asks me to scan my finger.
 *    I scan my finger.
 *  - The system compares the finger I just scanned to the one that was
 *    enrolled earlier. If the system decides that the fingerprints match,
 *    I am successfully logged in. Otherwise, the system informs me that I am
 *    not authorised to login as that user.
 *
 * \section identification Identification
 *
 * Identification is the process of comparing a freshly scanned fingerprint
 * to a <em>collection</em> of previously enrolled fingerprints. For example,
 * imagine there are 100 people in an organisation, and they all have enrolled
 * their fingerprints. One user walks up to a fingerprint scanner and scans
 * their finger. With <em>no other knowledge</em> of who that user might be,
 * the system examines their fingerprint, looks in the database, and determines
 * that the user is user number #61.
 *
 * In other words, verification might be seen as a one-to-one fingerprint
 * comparison where you know the identity of the user that you wish to
 * authenticate, whereas identification is a one-to-many comparison where you
 * do not know the identity of the user that you wish to authenticate.
 *
 * \section compat_general Device and print compatibility
 * Moving off generic conceptual ideas and onto libfprint-specific
 * implementation details, here are some introductory notes regarding how
 * libfprint copes with compatibility of fingerprints.
 *
 * libfprint deals with a whole variety of different fingerprint readers and
 * the design includes considerations of compatibility and interoperability
 * between multiple devices. Your application should also be prepared to
 * work with more than one type of fingerprint reader and should consider that
 * enrolled fingerprint X may not be compatible with the device the user has
 * plugged in today.
 *
 * libfprint implements the principle that fingerprints from different devices
 * are not necessarily compatible. For example, different devices may see
 * significantly different areas of fingerprint surface, and comparing images
 * between the devices would be unreliable. Also, devices can stretch and
 * distort images in different ways.
 *
 * libfprint also implements the principle that in some cases, fingerprints
 * <em>are</em> compatible between different devices. If you go and buy two
 * identical fingerprint readers, it seems logical that you should be able
 * to enroll on one and verify on another without problems.
 *
 * libfprint takes a fairly simplistic approach to these issues. Internally,
 * fingerprint hardware is driven by individual drivers. libfprint enforces
 * that a fingerprint that came from a device backed by driver X is never
 * compared to a fingerprint that came from a device backed by driver Y.
 *
 * Additionally, libfprint is designed for the situation where a single driver
 * may support a range of devices which differ in imaging or scanning
 * properties. For example, a driver may support two ranges of devices which
 * even though are programmed over the same interface, one device sees
 * substantially less of the finger flesh, therefore images from the two
 * device types should be incompatible despite being from the same driver. To
 * implement this, each driver assigns a <em>device type</em> to each device
 * that it detects based on its imaging characteristics. libfprint ensures that
 * two prints being compared have the same device type.
 *
 * In summary, libfprint represents fingerprints in several internal structures
 * and each representation will offer you a way of determining the
 * \ref driver_id "driver ID" and \ref devtype "devtype" of the print in
 * question. Prints are only compatible if the driver ID <b>and</b> devtypes
 * match. libfprint does offer you some "is this print compatible?" helper
 * functions, so you don't have to worry about these details too much.
 *
 * \section sync Synchronity/asynchronity
 *
 * Currently, all data acquisition operations are synchronous and can
 * potentially block for extended periods of time. For example, the enroll
 * function will block for an unpredictable amount of time until the user
 * scans their finger.
 *
 * Alternative asynchronous/non-blocking functionality will be offered in
 * future but has not been implemented yet.
 *
 * \section getting_started Getting started
 *
 * libfprint includes several simple functional examples under the examples/
 * directory in the libfprint source distribution. Those are good starting
 * points.
 *
 * Usually the first thing you want to do is determine which fingerprint
 * devices are present. This is done through \ref dscv_dev "device discovery".
 *
 * Once you have found a device you would like to operate, you should open it.
 * Refer to \ref dev "device operations". This section also details enrollment,
 * image capture, and verification.
 *
 *
 * That should be enough to get you started, but do remember there are
 * documentation pages on other aspects of libfprint's API (see the modules
 * page).
 */

/** @defgroup core Core library operations */

/**
 * @defgroup dev Device operations
 * In order to interact with fingerprint scanners, your software will
 * interface primarily with libfprint's representation of devices, detailed
 * on this page.
 *
 * \section enrolling Enrolling
 * Enrolling is represented within libfprint as a multi-stage process. This
 * slightly complicates things for application developers, but is required
 * for a smooth process.
 *
 * Some devices require the user to scan their finger multiple times in
 * order to complete the enrollment process. libfprint must return control
 * to your application inbetween each scan in order for your application to
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
 * \section imaging Imaging
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
 *
 * \section devtype Devtypes
 * Internally, the \ref drv "driver" behind a device assigns a 32-bit
 * <em>devtype</em> identifier to the device. This cannot be used as a unique
 * ID for a specific device as many devices under the same range may share
 * the same devtype. The devtype may even be 0 in all cases.
 *
 * The only reason you may be interested in retrieving the devtype for a
 * device is for the purpose of checking if some print data is compatible
 * with a device. libfprint uses the devtype as one way of checking that the
 * print you are verifying is compatible with the device in question - the
 * devtypes must be equal. This effectively allows drivers to support more
 * than one type of device where the data from each one is not compatible with
 * the other. Note that libfprint does provide you with helper functions to
 * determine whether a print is compatible with a device, so under most
 * circumstances, you don't have to worry about devtypes at all.
 */

/** @defgroup dscv_dev Device discovery
 * These functions allow you to scan the system for supported fingerprint
 * scanning hardware. This is your starting point when integrating libfprint
 * into your software.
 *
 * When you've identified a discovered device that you would like to control,
 * you can open it with fp_dev_open(). Note that discovered devices may no
 * longer be available at the time when you want to open them, for example
 * the user may have unplugged the device.
 */

/** @defgroup drv Driver operations
 * Internally, libfprint is abstracted into various drivers to communicate
 * with the different types of supported fingerprint readers. libfprint works
 * hard so that you don't have to care about these internal abstractions,
 * however there are some situations where you may be interested in a little
 * behind-the-scenes driver info.
 *
 * You can obtain the driver for a device using fp_dev_get_driver(), which
 * you can pass to the functions documented on this page.
 *
 * \section driver_id Driver IDs
 * Each driver is assigned a unique ID by the project maintainer. These
 * assignments are
 * <a href="http://www.reactivated.net/fprint/Driver_ID_assignments">
 * documented on the wiki</a> and will never change.
 *
 * The only reason you may be interested in retrieving the driver ID for a
 * driver is for the purpose of checking if some print data is compatible
 * with a device. libfprint uses the driver ID as one way of checking that
 * the print you are trying to verify is compatible with the device in
 * question - it ensures that enrollment data from one driver is never fed to
 * another. Note that libfprint does provide you with helper functions to
 * determine whether a print is compatible with a device, so under most
 * circumstances, you don't have to worry about driver IDs at all.
 */

static GSList *registered_drivers = NULL;
static GSList *opened_devices = NULL;

void fpi_log(enum fpi_log_level level, const char *component,
	const char *function, const char *format, ...)
{
	va_list args;
	FILE *stream = stdout;
	const char *prefix;

	switch (level) {
	case LOG_LEVEL_INFO:
		prefix = "info";
		break;
	case LOG_LEVEL_WARNING:
		stream = stderr;
		prefix = "warning";
		break;
	case LOG_LEVEL_ERROR:
		stream = stderr;
		prefix = "error";
		break;
	case LOG_LEVEL_DEBUG:
		stream = stderr;
		prefix = "debug";
		break;
	default:
		stream = stderr;
		prefix = "unknown";
		break;
	}

	fprintf(stream, "%s:%s [%s] ", component ? component : "fp", prefix,
		function);

	va_start (args, format);
	vfprintf(stream, format, args);
	va_end (args);

	fprintf(stream, "\n");
}

static void register_driver(struct fp_driver *drv)
{
	if (drv->id == 0) {
		fp_err("not registering driver %s: driver ID is 0");
		return;
	}
	registered_drivers = g_slist_prepend(registered_drivers, (gpointer) drv);
	fp_dbg("registered driver %s", drv->name);
}

static struct fp_driver * const primitive_drivers[] = {
	&upekts_driver,
};

static struct fp_img_driver * const img_drivers[] = {
	&uru4000_driver,
	&aes1610_driver,
	&aes2501_driver,
	&aes4000_driver,
	&upektc_driver,
	&fdu2000_driver,
};

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

static struct fp_driver *find_supporting_driver(struct usb_device *udev,
	const struct usb_id **usb_id)
{
	GSList *elem = registered_drivers;
	
	do {
		struct fp_driver *drv = elem->data;
		const struct usb_id *id;

		for (id = drv->id_table; id->vendor; id++)
			if (udev->descriptor.idVendor == id->vendor &&
					udev->descriptor.idProduct == id->product) {
				fp_dbg("driver %s supports USB device %04x:%04x",
					drv->name, id->vendor, id->product);
				*usb_id = id;
				return drv;
			}
	} while ((elem = g_slist_next(elem)));
	return NULL;
}

static struct fp_dscv_dev *discover_dev(struct usb_device *udev)
{
	const struct usb_id *usb_id;
	struct fp_driver *drv = find_supporting_driver(udev, &usb_id);
	struct fp_dscv_dev *ddev;
	uint32_t devtype = 0;

	if (!drv)
		return NULL;

	if (drv->discover) {
		int r = drv->discover(usb_id, &devtype);
		if (r < 0)
			fp_err("%s discover failed, code %d", drv->name, r);
		if (r <= 0)
			return NULL;
	}

	ddev = g_malloc0(sizeof(*ddev));
	ddev->drv = drv;
	ddev->udev = udev;
	ddev->driver_data = usb_id->driver_data;
	ddev->devtype = devtype;
	return ddev;
}

/** \ingroup dscv_dev
 * Scans the system and returns a list of discovered devices. This is your
 * entry point into finding a fingerprint reader to operate.
 * \returns a NULL-terminated list of discovered devices. Must be freed with
 * fp_dscv_devs_free() after use.
 */
API_EXPORTED struct fp_dscv_dev **fp_discover_devs(void)
{
	GSList *tmplist = NULL;
	struct fp_dscv_dev **list;
	struct usb_device *udev;
	struct usb_bus *bus;
	int dscv_count = 0;

	if (registered_drivers == NULL)
		return NULL;

	usb_find_busses();
	usb_find_devices();

	/* Check each device against each driver, temporarily storing successfully
	 * discovered devices in a GSList.
	 *
	 * Quite inefficient but excusable as we'll only be dealing with small
	 * sets of drivers against small sets of USB devices */
	for (bus = usb_get_busses(); bus; bus = bus->next)
		for (udev = bus->devices; udev; udev = udev->next) {
			struct fp_dscv_dev *ddev = discover_dev(udev);
			if (!ddev)
				continue;
			tmplist = g_slist_prepend(tmplist, (gpointer) ddev);
			dscv_count++;
		}

	/* Convert our temporary GSList into a standard NULL-terminated pointer
	 * array. */
	list = g_malloc(sizeof(*list) * (dscv_count + 1));
	if (dscv_count > 0) {
		GSList *elem = tmplist;
		int i = 0;
		do {
			list[i++] = elem->data;
		} while ((elem = g_slist_next(elem)));
	}
	list[dscv_count] = NULL; /* NULL-terminate */

	g_slist_free(tmplist);
	return list;
}

/** \ingroup dscv_dev
 * Free a list of discovered devices. This function destroys the list and all
 * discovered devices that it included, so make sure you have opened your
 * discovered device <b>before</b> freeing the list.
 * \param devs the list of discovered devices. If NULL, function simply
 * returns.
 */
API_EXPORTED void fp_dscv_devs_free(struct fp_dscv_dev **devs)
{
	int i;
	if (!devs)
		return;

	for (i = 0; devs[i]; i++)
		g_free(devs[i]);
	g_free(devs);
}

/** \ingroup dscv_dev
 * Gets the \ref drv "driver" for a discovered device.
 * \param dev the discovered device
 * \returns the driver backing the device
 */
API_EXPORTED struct fp_driver *fp_dscv_dev_get_driver(struct fp_dscv_dev *dev)
{
	return dev->drv;
}

/** \ingroup dscv_dev
 * Gets the \ref devtype "devtype" for a discovered device.
 * \param dev the discovered device
 * \returns the devtype of the device
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

/** \ingroup dscv_dev
 * Determines if a specific \ref print_data "stored print" appears to be
 * compatible with a discovered device.
 * \param dev the discovered device
 * \param data the print for compatibility checking
 * \returns 1 if the print is compatible with the device, 0 otherwise
 */
API_EXPORTED int fp_dscv_dev_supports_print_data(struct fp_dscv_dev *dev,
	struct fp_print_data *data)
{
	return fpi_print_data_compatible(dev->drv->id, dev->devtype,
		fpi_driver_get_data_type(dev->drv), data->driver_id, data->devtype,
		data->type);
}

/** \ingroup dscv_dev
 * Determines if a specific \ref dscv_print "discovered print" appears to be
 * compatible with a discovered device.
 * \param dev the discovered device
 * \param data the discovered print for compatibility checking
 * \returns 1 if the print is compatible with the device, 0 otherwise
 */
API_EXPORTED int fp_dscv_dev_supports_dscv_print(struct fp_dscv_dev *dev,
	struct fp_dscv_print *data)
{
	return fpi_print_data_compatible(dev->drv->id, dev->devtype, 0,
		data->driver_id, data->devtype, 0);
}

/** \ingroup dscv_dev
 * Searches a list of discovered devices for a device that appears to be
 * compatible with a \ref print_data "stored print".
 * \param devs a list of discovered devices
 * \param data the print under inspection
 * \returns the first discovered device that appears to support the print, or
 * NULL if no apparently compatible devices could be found
 */
API_EXPORTED struct fp_dscv_dev *fp_dscv_dev_for_print_data(struct fp_dscv_dev **devs,
	struct fp_print_data *data)
{
	struct fp_dscv_dev *ddev;
	int i;

	for (i = 0; (ddev = devs[i]); i++)
		if (fp_dscv_dev_supports_print_data(ddev, data))
			return ddev;
	return NULL;
}

/** \ingroup dscv_dev
 * Searches a list of discovered devices for a device that appears to be
 * compatible with a \ref dscv_print "discovered print".
 * \param devs a list of discovered devices
 * \param print the print under inspection
 * \returns the first discovered device that appears to support the print, or
 * NULL if no apparently compatible devices could be found
 */
API_EXPORTED struct fp_dscv_dev *fp_dscv_dev_for_dscv_print(struct fp_dscv_dev **devs,
	struct fp_dscv_print *print)
{
	struct fp_dscv_dev *ddev;
	int i;

	for (i = 0; (ddev = devs[i]); i++)
		if (fp_dscv_dev_supports_dscv_print(ddev, print))
			return ddev;
	return NULL;
}

/** \ingroup dev
 * Opens and initialises a device. This is the function you call in order
 * to convert a \ref dscv_dev "discovered device" into an actual device handle
 * that you can perform operations with.
 * \param ddev the discovered device to open
 * \returns the opened device handle, or NULL on error
 */
API_EXPORTED struct fp_dev *fp_dev_open(struct fp_dscv_dev *ddev)
{
	struct fp_dev *dev;
	struct fp_driver *drv = ddev->drv;
	int r;

	usb_dev_handle *udevh = usb_open(ddev->udev);
	if (!udevh) {
		fp_err("usb_open failed");
		return NULL;
	}

	dev = g_malloc0(sizeof(*dev));
	dev->drv = drv;
	dev->udev = udevh;
	dev->__enroll_stage = -1;

	if (drv->init) {
		r = drv->init(dev, ddev->driver_data);
		if (r) {
			fp_err("device initialisation failed, driver=%s", drv->name);
			usb_close(udevh);
			g_free(dev);
			return NULL;
		}
	}

	fp_dbg("");
	opened_devices = g_slist_prepend(opened_devices, (gpointer) dev);
	return dev;
}

/* performs close operation without modifying opened_devices list */
static void do_close(struct fp_dev *dev)
{
	if (dev->drv->exit)
		dev->drv->exit(dev);
	usb_close(dev->udev);
	g_free(dev);
}

/** \ingroup dev
 * Close a device. You must call this function when you are finished using
 * a fingerprint device.
 * \param dev the device to close. If NULL, function simply returns.
 */
API_EXPORTED void fp_dev_close(struct fp_dev *dev)
{
	if (!dev)
		return;

	fp_dbg("");

	if (g_slist_index(opened_devices, (gconstpointer) dev) == -1)
		fp_err("device %p not in opened list!", dev);
	opened_devices = g_slist_remove(opened_devices, (gconstpointer) dev);
	do_close(dev);
}

/** \ingroup dev
 * Get the \ref drv "driver" for a fingerprint device.
 * \param dev the device
 * \returns the driver controlling the device
 */
API_EXPORTED struct fp_driver *fp_dev_get_driver(struct fp_dev *dev)
{
	return dev->drv;
}

/** \ingroup dev
 * Gets the number of \ref enrolling "enroll stages" required to enroll a
 * fingerprint with the device.
 * \param dev the device
 * \returns the number of enroll stages
 */
API_EXPORTED int fp_dev_get_nr_enroll_stages(struct fp_dev *dev)
{
	return dev->nr_enroll_stages;
}

/** \ingroup dev
 * Gets the \ref devtype "devtype" for a device.
 * \param dev the device
 * \returns the devtype
 */
API_EXPORTED uint32_t fp_dev_get_devtype(struct fp_dev *dev)
{
	return dev->devtype;
}

/** \ingroup dev
 * Determines if a stored print is compatible with a certain device.
 * \param dev the device
 * \param data the stored print
 * \returns 1 if the print is compatible with the device, 0 if not
 */
API_EXPORTED int fp_dev_supports_print_data(struct fp_dev *dev,
	struct fp_print_data *data)
{
	return fpi_print_data_compatible(dev->drv->id, dev->devtype,
		fpi_driver_get_data_type(dev->drv), data->driver_id, data->devtype,
		data->type);
}

/** \ingroup dev
 * Determines if a \ref dscv_print "discovered print" appears to be compatible
 * with a certain device.
 * \param dev the device
 * \param data the discovered print
 * \returns 1 if the print is compatible with the device, 0 if not
 */
API_EXPORTED int fp_dev_supports_dscv_print(struct fp_dev *dev,
	struct fp_dscv_print *data)
{
	return fpi_print_data_compatible(dev->drv->id, dev->devtype,
		0, data->driver_id, data->devtype, 0);
}

/** \ingroup drv
 * Retrieves the name of the driver. For example: "upekts"
 * \param drv the driver
 * \returns the driver name. Must not be modified or freed.
 */
API_EXPORTED const char *fp_driver_get_name(struct fp_driver *drv)
{
	return drv->name;
}

/** \ingroup drv
 * Retrieves a descriptive name of the driver. For example: "UPEK TouchStrip"
 * \param drv the driver
 * \returns the descriptive name. Must not be modified or freed.
 */
API_EXPORTED const char *fp_driver_get_full_name(struct fp_driver *drv)
{
	return drv->full_name;
}

/** \ingroup drv
 * Retrieves the driver ID code for a driver.
 * \param drv the driver
 * \returns the driver ID
 */
API_EXPORTED uint16_t fp_driver_get_driver_id(struct fp_driver *drv)
{
	return drv->id;
}

static struct fp_img_dev *dev_to_img_dev(struct fp_dev *dev)
{
	if (dev->drv->type != DRIVER_IMAGING)
		return NULL;
	return dev->priv;
}

/** \ingroup dev
 * Determines if a device has imaging capabilities. If a device has imaging
 * capabilities you are able to perform imaging operations such as retrieving
 * scan images using fp_dev_img_capture(). However, not all devices are
 * imaging devices - some do all processing in hardware. This function will
 * indicate which class a device in question falls into.
 * \param dev the fingerprint device
 * \returns 1 if the device is an imaging device, 0 if the device does not
 * provide images to the host computer
 */
API_EXPORTED int fp_dev_supports_imaging(struct fp_dev *dev)
{
	return dev->drv->type == DRIVER_IMAGING;
}

/** \ingroup dev
 * Determines if a device is capable of \ref identification "identification"
 * through fp_identify_finger() and similar. Not all devices support this
 * functionality.
 * \param dev the fingerprint device
 * \returns 1 if the device is capable of identification, 0 otherwise.
 */
API_EXPORTED int fp_dev_supports_identification(struct fp_dev *dev)
{
	return dev->drv->identify != NULL;
}

/** \ingroup dev
 * Captures an \ref img "image" from a device. The returned image is the raw
 * image provided by the device, you may wish to \ref img_std "standardize" it.
 *
 * If set, the <tt>unconditional</tt> flag indicates that the device should
 * capture an image unconditionally, regardless of whether a finger is there
 * or not. If unset, this function will block until a finger is detected on
 * the sensor.
 *
 * \param dev the device
 * \param unconditional whether to unconditionally capture an image, or to only capture when a finger is detected
 * \param image a location to return the captured image. Must be freed with
 * fp_img_free() after use.
 * \return 0 on success, non-zero on error. -ENOTSUP indicates that either the
 * unconditional flag was set but the device does not support this, or that the
 * device does not support imaging.
 * \sa fp_dev_supports_imaging()
 */
API_EXPORTED int fp_dev_img_capture(struct fp_dev *dev, int unconditional,
	struct fp_img **image)
{
	struct fp_img_dev *imgdev = dev_to_img_dev(dev);
	if (!imgdev) {
		fp_dbg("image capture on non-imaging device");
		return -ENOTSUP;
	}

	return fpi_imgdev_capture(imgdev, unconditional, image);
}

/** \ingroup dev
 * Gets the expected width of images that will be captured from the device.
 * This function will return -1 for devices that are not
 * \ref imaging "imaging devices". If the width of images from this device
 * can vary, 0 will be returned.
 * \param dev the device
 * \returns the expected image width, or 0 for variable, or -1 for non-imaging
 * devices.
 */
API_EXPORTED int fp_dev_get_img_width(struct fp_dev *dev)
{
	struct fp_img_dev *imgdev = dev_to_img_dev(dev);
	if (!imgdev) {
		fp_dbg("get image width for non-imaging device");
		return -1;
	}

	return fpi_imgdev_get_img_width(imgdev);
}

/** \ingroup dev
 * Gets the expected height of images that will be captured from the device.
 * This function will return -1 for devices that are not
 * \ref imaging "imaging devices". If the height of images from this device
 * can vary, 0 will be returned.
 * \param dev the device
 * \returns the expected image height, or 0 for variable, or -1 for non-imaging
 * devices.
 */
API_EXPORTED int fp_dev_get_img_height(struct fp_dev *dev)
{
	struct fp_img_dev *imgdev = dev_to_img_dev(dev);
	if (!imgdev) {
		fp_dbg("get image height for non-imaging device");
		return -1;
	}

	return fpi_imgdev_get_img_height(imgdev);
}

/** \ingroup dev
 * Performs an enroll stage. See \ref enrolling for an explanation of enroll
 * stages.
 *
 * If no enrollment is in process, this kicks of the process and runs the
 * first stage. If an enrollment is already in progress, calling this
 * function runs the next stage, which may well be the last.
 *
 * A negative error code may be returned from any stage. When this occurs,
 * further calls to the enroll function will start a new enrollment process,
 * i.e. a negative error code indicates that the enrollment process has been
 * aborted. These error codes only ever indicate unexpected internal errors
 * or I/O problems.
 *
 * The RETRY codes from #fp_enroll_result may be returned from any enroll
 * stage. These codes indicate that the scan was not succesful in that the
 * user did not position their finger correctly or similar. When a RETRY code
 * is returned, the enrollment stage is <b>not</b> advanced, so the next call
 * into this function will retry the current stage again. The current stage may
 * need to be retried several times.
 *
 * The fp_enroll_result#FP_ENROLL_FAIL code may be returned from any enroll
 * stage. This code indicates that even though the scans themselves have been
 * acceptable, data processing applied to these scans produces incomprehensible
 * results. In other words, the user may have been scanning a different finger
 * for each stage or something like that. Like negative error codes, this
 * return code indicates that the enrollment process has been aborted.
 *
 * The fp_enroll_result#FP_ENROLL_PASS code will only ever be returned for
 * non-final stages. This return code indicates that the scan was acceptable
 * and the next call into this function will advance onto the next enroll
 * stage.
 *
 * The fp_enroll_result#FP_ENROLL_COMPLETE code will only ever be returned
 * from the final enroll stage. It indicates that enrollment completed
 * successfully, and that print_data has been assigned to point to the
 * resultant enrollment data. The print_data parameter will not be modified
 * during any other enrollment stages, hence it is actually legal to pass NULL
 * as this argument for all but the final stage.
 * 
 * If the device is an imaging device, it can also return the image from
 * the scan, even when the enroll fails with a RETRY or FAIL code. It is legal
 * to call this function even on non-imaging devices, just don't expect them to
 * provide images.
 *
 * \param dev the device
 * \param print_data a location to return the resultant enrollment data from
 * the final stage. Must be freed with fp_print_data_free() after use.
 * \param img location to store the scan image. accepts NULL for no image
 * storage. If an image is returned, it must be freed with fp_img_free() after
 * use.
 * \return negative code on error, otherwise a code from #fp_enroll_result
 */
API_EXPORTED int fp_enroll_finger_img(struct fp_dev *dev,
	struct fp_print_data **print_data, struct fp_img **img)
{
	struct fp_driver *drv = dev->drv;
	struct fp_img *_img = NULL;
	int ret;
	int stage = dev->__enroll_stage;
	gboolean initial = FALSE;

	if (!dev->nr_enroll_stages || !drv->enroll) {
		fp_err("driver %s has 0 enroll stages or no enroll func",
			drv->name);
		return -ENOTSUP;
	}

	if (stage == -1) {
		initial = TRUE;
		dev->__enroll_stage = ++stage;
	}

	if (stage >= dev->nr_enroll_stages) {
		fp_err("exceeding number of enroll stages for device claimed by "
			"driver %s (%d stages)", drv->name, dev->nr_enroll_stages);
		dev->__enroll_stage = -1;
		return -EINVAL;
	}
	fp_dbg("%s will handle enroll stage %d/%d%s", drv->name, stage,
		dev->nr_enroll_stages - 1, initial ? " (initial)" : "");

	ret = drv->enroll(dev, initial, stage, print_data, &_img);
	if (ret < 0) {
		fp_err("enroll failed with code %d", ret);
		dev->__enroll_stage = -1;
		return ret;
	}

	if (img)
		*img = _img;
	else
		fp_img_free(_img);

	switch (ret) {
	case FP_ENROLL_PASS:
		fp_dbg("enroll stage passed");
		dev->__enroll_stage = stage + 1;
		break;
	case FP_ENROLL_COMPLETE:
		fp_dbg("enroll complete");
		dev->__enroll_stage = -1;
		break;
	case FP_ENROLL_RETRY:
		fp_dbg("enroll should retry");
		break;
	case FP_ENROLL_RETRY_TOO_SHORT:
		fp_dbg("swipe was too short, enroll should retry");
		break;
	case FP_ENROLL_RETRY_CENTER_FINGER:
		fp_dbg("finger was not centered, enroll should retry");
		break;
	case FP_ENROLL_RETRY_REMOVE_FINGER:
		fp_dbg("scan failed, remove finger and retry");
		break;
	case FP_ENROLL_FAIL:
		fp_err("enroll failed");
		dev->__enroll_stage = -1;
		break;
	default:
		fp_err("unrecognised return code %d", ret);
		dev->__enroll_stage = -1;
		return -EINVAL;
	}
	return ret;
}

/** \ingroup dev
 * Performs a new scan and verify it against a previously enrolled print.
 * If the device is an imaging device, it can also return the image from
 * the scan, even when the verify fails with a RETRY code. It is legal to
 * call this function even on non-imaging devices, just don't expect them to
 * provide images.
 *
 * \param dev the device to perform the scan.
 * \param enrolled_print the print to verify against. Must have been previously
 * enrolled with a device compatible to the device selected to perform the scan.
 * \param img location to store the scan image. accepts NULL for no image
 * storage. If an image is returned, it must be freed with fp_img_free() after
 * use.
 * \return negative code on error, otherwise a code from #fp_verify_result
 */
API_EXPORTED int fp_verify_finger_img(struct fp_dev *dev,
	struct fp_print_data *enrolled_print, struct fp_img **img)
{
	struct fp_driver *drv = dev->drv;
	struct fp_img *_img = NULL;
	int r;

	if (!enrolled_print) {
		fp_err("no print given");
		return -EINVAL;
	}

	if (!drv->verify) {
		fp_err("driver %s has no verify func", drv->name);
		return -EINVAL;
	}

	if (!fp_dev_supports_print_data(dev, enrolled_print)) {
		fp_err("print is not compatible with device");
		return -EINVAL;
	}

	fp_dbg("to be handled by %s", drv->name);
	r = drv->verify(dev, enrolled_print, &_img);
	if (r < 0) {
		fp_dbg("verify error %d", r);
		return r;
	}

	if (img)
		*img = _img;
	else
		fp_img_free(_img);

	switch (r) {
	case FP_VERIFY_NO_MATCH:
		fp_dbg("result: no match");
		break;
	case FP_VERIFY_MATCH:
		fp_dbg("result: match");
		break;
	case FP_VERIFY_RETRY:
		fp_dbg("verify should retry");
		break;
	case FP_VERIFY_RETRY_TOO_SHORT:
		fp_dbg("swipe was too short, verify should retry");
		break;
	case FP_VERIFY_RETRY_CENTER_FINGER:
		fp_dbg("finger was not centered, verify should retry");
		break;
	case FP_VERIFY_RETRY_REMOVE_FINGER:
		fp_dbg("scan failed, remove finger and retry");
		break;
	default:
		fp_err("unrecognised return code %d", r);
		return -EINVAL;
	}

	return r;
}

/** \ingroup dev
 * Performs a new scan and attempts to identify the scanned finger against
 * a collection of previously enrolled fingerprints.
 * If the device is an imaging device, it can also return the image from
 * the scan, even when identification fails with a RETRY code. It is legal to
 * call this function even on non-imaging devices, just don't expect them to
 * provide images.
 *
 * This function returns codes from #fp_verify_result. The return code
 * fp_verify_result#FP_VERIFY_MATCH indicates that the scanned fingerprint
 * does appear in the print gallery, and the match_offset output parameter
 * will indicate the index into the print gallery array of the matched print.
 *
 * This function will not necessarily examine the whole print gallery, it
 * will return as soon as it finds a matching print.
 *
 * Not all devices support identification. -ENOTSUP will be returned when
 * this is the case.
 *
 * \param dev the device to perform the scan.
 * \param print_gallery NULL-terminated array of pointers to the prints to
 * identify against. Each one must have been previously enrolled with a device
 * compatible to the device selected to perform the scan.
 * \param match_offset output location to store the array index of the matched
 * gallery print (if any was found). Only valid if FP_VERIFY_MATCH was
 * returned.
 * \param img location to store the scan image. accepts NULL for no image
 * storage. If an image is returned, it must be freed with fp_img_free() after
 * use.
 * \return negative code on error, otherwise a code from #fp_verify_result
 */
API_EXPORTED int fp_identify_finger_img(struct fp_dev *dev,
	struct fp_print_data **print_gallery, size_t *match_offset,
	struct fp_img **img)
{
	struct fp_driver *drv = dev->drv;
	struct fp_img *_img;
	int r;

	if (!drv->identify) {
		fp_dbg("driver %s has no identify func", drv->name);
		return -ENOTSUP;
	}
	fp_dbg("to be handled by %s", drv->name);
	r = drv->identify(dev, print_gallery, match_offset, &_img);
	if (r < 0) {
		fp_dbg("identify error %d", r);
		return r;
	}

	if (img)
		*img = _img;
	else
		fp_img_free(_img);

	switch (r) {
	case FP_VERIFY_NO_MATCH:
		fp_dbg("result: no match");
		break;
	case FP_VERIFY_MATCH:
		fp_dbg("result: match at offset %zd", match_offset);
		break;
	case FP_VERIFY_RETRY:
		fp_dbg("verify should retry");
		break;
	case FP_VERIFY_RETRY_TOO_SHORT:
		fp_dbg("swipe was too short, verify should retry");
		break;
	case FP_VERIFY_RETRY_CENTER_FINGER:
		fp_dbg("finger was not centered, verify should retry");
		break;
	case FP_VERIFY_RETRY_REMOVE_FINGER:
		fp_dbg("scan failed, remove finger and retry");
		break;
	default:
		fp_err("unrecognised return code %d", r);
		return -EINVAL;
	}

	return r;
}

/** \ingroup core
 * Initialise libfprint. This function must be called before you attempt to
 * use the library in any way.
 * \return 0 on success, non-zero on error.
 */
API_EXPORTED int fp_init(void)
{
	fp_dbg("");
	usb_init();
	register_drivers();
	return 0;
}

/** \ingroup core
 * Deinitialise libfprint. This function should be called during your program
 * exit sequence. You must not use any libfprint functions after calling this
 * function, unless you call fp_init() again.
 */
API_EXPORTED void fp_exit(void)
{
	GSList *elem = opened_devices;
	fp_dbg("");

	if (elem != NULL) {
		do {
			fp_dbg("naughty app left a device open on exit!");
			do_close((struct fp_dev *) elem->data);
		} while ((elem = g_slist_next(elem)));
		g_slist_free(opened_devices);
		opened_devices = NULL;
	}

	fpi_data_exit();
	g_slist_free(registered_drivers);
	registered_drivers = NULL;
}

