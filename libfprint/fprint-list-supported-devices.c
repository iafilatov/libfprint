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

GHashTable *printed = NULL;

static void print_driver (struct fp_driver *driver)
{
    int i;

    for (i = 0; driver->id_table[i].vendor != 0; i++) {
        char *key;

	key = g_strdup_printf ("%04x:%04x", driver->id_table[i].vendor, driver->id_table[i].product);

	if (g_hash_table_lookup (printed, key) != NULL) {
	    g_free (key);
	    continue;
	}

	g_hash_table_insert (printed, key, GINT_TO_POINTER (1));

	g_print ("%s | %s\n", key, driver->full_name);
    }
}

int main (int argc, char **argv)
{
    struct fp_driver **list;
    guint i;

    list = fprint_get_drivers ();

    printed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    g_print ("# Supported Devices\n");
    g_print ("\n");
    g_print ("## USB devices\n");
    g_print ("\n");
    g_print ("USB ID | Driver\n");
    g_print ("------------ | ------------\n");

    for (i = 0; list[i] != NULL; i++) {
	print_driver (list[i]);
    }

    g_hash_table_destroy (printed);

    return 0;
}
