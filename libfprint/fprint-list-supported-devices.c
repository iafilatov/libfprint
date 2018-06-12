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
#include <locale.h>

#include "fp_internal.h"

GHashTable *printed = NULL;

static GList *insert_driver (GList *list,
			   struct fp_driver *driver)
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

	list = g_list_prepend (list, g_strdup_printf ("%s | %s\n", key, driver->full_name));
    }

    return list;
}

int main (int argc, char **argv)
{
    struct fp_driver **driver_list;
    guint i;
    GList *list, *l;

    setlocale (LC_ALL, "");

    driver_list = fprint_get_drivers ();

    printed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    g_print ("%% lifprint — Supported Devices\n");
    g_print ("%% Bastien Nocera, Daniel Drake\n");
    g_print ("%% 2018\n");
    g_print ("\n");

    g_print ("# Supported Devices\n");
    g_print ("\n");
    g_print ("This is a list of supported devices in libfprint's development version. Those drivers might not all be available in the stable, released version. If in doubt, contact your distribution or systems integrator for details.\n");
    g_print ("\n");
    g_print ("## USB devices\n");
    g_print ("\n");
    g_print ("USB ID | Driver\n");
    g_print ("------------ | ------------\n");

    list = NULL;
    for (i = 0; driver_list[i] != NULL; i++)
	list = insert_driver (list, driver_list[i]);

    list = g_list_sort (list, (GCompareFunc) g_strcmp0);
    for (l = list; l != NULL; l = l->next)
        g_print ("%s", (char *) l->data);

    g_list_free_full (list, g_free);
    g_hash_table_destroy (printed);

    return 0;
}
