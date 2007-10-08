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

#include <glib.h>
#include <usb.h>

#include "fp_internal.h"

static GList *registered_drivers = NULL;

static void register_driver(const struct fp_driver *drv)
{
	registered_drivers = g_list_prepend(registered_drivers, (gpointer) drv);
}

static const struct fp_driver * const drivers[] = {
	&upekts_driver,
};

static void register_drivers(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(drivers); i++)
		register_driver(drivers[i]);
}

static const struct fp_driver *find_supporting_driver(struct usb_device *udev)
{
	GList *elem = registered_drivers;
	
	do {
		const struct fp_driver *drv = elem->data;
		const struct usb_id *id;

		for (id = drv->id_table; id->vendor; id++)
			if (udev->descriptor.idVendor == id->vendor &&
					udev->descriptor.idProduct == id->product)
				return drv;
	} while (elem = g_list_next(elem));
	return NULL;
}

API_EXPORTED struct fp_dscv_dev **fp_discover_devs(void)
{
	GList *tmplist = NULL;
	struct fp_dscv_dev **list;
	struct usb_device *udev;
	struct usb_bus *bus;
	int dscv_count = 0;

	if (registered_drivers == NULL)
		return NULL;

	usb_find_busses();
	usb_find_devices();

	/* Check each device against each driver, temporarily storing successfully
	 * discovered devices in a GList.
	 *
	 * Quite inefficient but excusable as we'll only be dealing with small
	 * sets of drivers against small sets of USB devices */
	for (bus = usb_get_busses(); bus; bus = bus->next)
		for (udev = bus->devices; udev; udev = udev->next) {
			const struct fp_driver *drv = find_supporting_driver(udev);
			struct fp_dscv_dev *ddev;
			if (!drv)
				continue;
			ddev = g_malloc0(sizeof(*ddev));
			ddev->drv = drv;
			ddev->udev = udev;
			tmplist = g_list_prepend(tmplist, (gpointer) ddev);
			dscv_count++;
		}

	/* Convert our temporary GList into a standard NULL-terminated pointer
	 * array. */
	list = g_malloc(sizeof(*list) * (dscv_count + 1));
	if (dscv_count > 0) {
		GList *elem = tmplist;
		int i = 0;
		do {
			list[i++] = elem->data;
		} while (elem = g_list_next(elem));
	}
	list[dscv_count] = NULL; /* NULL-terminate */

	g_list_free(tmplist);
	return list;
}

API_EXPORTED void fp_dscv_devs_free(struct fp_dscv_dev **devs)
{
	int i;
	if (!devs)
		return;

	for (i = 0; devs[i]; i++)
		g_free(devs[i]);
	g_free(devs);
}

API_EXPORTED struct fp_dev *fp_dev_open(struct fp_dscv_dev *ddev)
{
	struct fp_dev *dev;
	const struct fp_driver *drv = ddev->drv;
	int r;

	usb_dev_handle *udevh = usb_open(ddev->udev);
	if (!udevh)
		return NULL;
	
	dev = g_malloc0(sizeof(*dev));
	dev->drv = drv;
	dev->udev = udevh;

	if (drv->init) {
		r = drv->init(dev);
		if (r) {
			usb_close(udevh);
			g_free(dev);
			return NULL;
		}
	}

	return dev;
}

API_EXPORTED void fp_dev_close(struct fp_dev *dev)
{
	if (dev->drv->exit)
		dev->drv->exit(dev);
	usb_close(dev->udev);
	g_free(dev);
}

API_EXPORTED int fp_init(void)
{
	usb_init();
	register_drivers();
	return 0;
}

