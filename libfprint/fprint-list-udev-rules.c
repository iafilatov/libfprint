/*
 * Copyright (C) 2009 Red Hat <mjg@redhat.com> 
 * Copyright (C) 2008 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2008 Timo Hoenig <thoenig@suse.de>, <thoenig@nouse.net>
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
#include <stdio.h>

#include "fp_internal.h"

static const struct usb_id whitelist_id_table[] = {
    { .vendor = 0x08ff, .product = 0x2810 },
    { 0, 0, 0, },
};

static const struct usb_id blacklist_id_table[] = {
    { .vendor = 0x0483, .product = 0x2016 },
    { 0, 0, 0 },
};

struct fp_driver whitelist = {
    .id_table = whitelist_id_table,
};

static void print_driver (struct fp_driver *driver)
{
    int i, j, blacklist;

    for (i = 0; driver->id_table[i].vendor != 0; i++) {
	blacklist = 0;
	for (j = 0; blacklist_id_table[j].vendor != 0; j++) {
	    if (driver->id_table[i].vendor == blacklist_id_table[j].vendor &&
		driver->id_table[j].product == blacklist_id_table[j].product) {
		blacklist = 1;
	    }
	}
	if (blacklist)
	    continue;

	printf ("SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"%04x\", ATTRS{idProduct}==\"%04x\", ATTRS{dev}==\"*\", ATTR{power/control}=\"auto\"\n", driver->id_table[i].vendor, driver->id_table[i].product);
    }
}

int main (int argc, char **argv)
{
    struct fp_driver **list;
    guint i;

    list = fprint_get_drivers ();

    for (i = 0; list[i] != NULL; i++) {
	print_driver (list[i]);
    }

    print_driver (&whitelist);

    return 0;
}
