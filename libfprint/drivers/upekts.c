/*
 * UPEK TouchStrip driver for libfprint
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

#include <fp_internal.h>

static const struct usb_id id_table[] = {
	{ .vendor = 0x0483, .product = 0x2016 },
	{ 0 }, /* terminating entry */
};

const struct fp_driver upekts_driver = {
	.name = "upekts",
	.full_name = "UPEK TouchStrip",
	.id_table = id_table,
};

