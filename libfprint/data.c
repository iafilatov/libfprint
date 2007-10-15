/*
 * Fingerprint data handling and storage
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
#include <string.h>

#include <glib.h>

#include "fp_internal.h"

struct fp_print_data *fpi_print_data_new(struct fp_dev *dev, size_t length)
{
	struct fp_print_data *data = g_malloc(sizeof(*data) + length);
	fp_dbg("length=%zd", length);
	data->driver_name = dev->drv->name;
	data->length = length;
	return data;
}

API_EXPORTED void fp_print_data_free(struct fp_print_data *data)
{
	g_free(data);
}

int fpi_print_data_compatible(struct fp_dev *dev, struct fp_print_data *data)
{
	return strcmp(dev->drv->name, data->driver_name) == 0;
}
