/*
 * Helper binary for creating a HAL FDI file for supported devices
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

/* FDI entry example:
 *
 *  <!-- AuthenTec AES2501 -->
 *  <match key="usb.vendor_id" int="0x08ff">
 *   <match key="usb.product_id" int="0x2580">
 *    <merge key="info.category" type="string">biometric.fingerprint_reader</merge>
 *    <append key="biometric.fingerprint_reader.access_method" type="strlist">libfprint</append>
 *    <append key="info.capabilities" type="strlist">biometric</append>
 *    <append key="info.capabilities" type="strlist">biometric.fingerprint_reader</append>
 *    <merge key="biometric.fingerprint_reader.libfprint.driver" type="string">aes2501</merge>
 *    <merge key="biometric.fingerprint_reader.libfprint.support" type="bool">true</merge>
 *   </match>
 *  </match>
 *
 */

static void print_driver (struct fp_driver *driver)
{
	int i;

	for (i = 0; driver->id_table[i].vendor != 0; i++) {
		printf ("  <!-- %s -->\n", fp_driver_get_full_name (driver));
		printf ("  <match key=\"usb.vendor_id\" int=\"0x%04x\">\n", driver->id_table[i].vendor);
		printf ("   <match key=\"usb.product_id\" int=\"0x%04x\">\n", driver->id_table[i].product);
		printf ("    <merge key=\"info.category\" type=\"string\">biometric.fingerprint_reader</merge>\n");
		printf ("    <append key=\"biometric.fingerprint_reader.access_method\" type=\"strlist\">libfprint</append>\n");
		printf ("    <append key=\"info.capabilities\" type=\"strlist\">biometric</append>\n");
		printf ("    <append key=\"info.capabilities\" type=\"strlist\">biometric.fingerprint_reader</append>\n");
		printf ("    <merge key=\"biometric.fingerprint_reader.libfprint.driver\" type=\"string\">%s</merge>\n", driver->name);
		printf ("    <merge key=\"biometric.fingerprint_reader.libfprint.support\" type=\"bool\">true</merge>\n");
		printf ("    <append key=\"biometric.fingerprint_reader.scan_type\" type=\"string\">%s</append>\n",
			fp_driver_get_scan_type (driver) == FP_SCAN_TYPE_PRESS ? "press" : "swipe");
		printf ("   </match>\n");
		printf ("  </match>\n");
	}
}

int main (int argc, char **argv)
{
	struct fp_driver **list;
	guint i;

	list = fprint_get_drivers ();

	printf ("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
	printf ("<!-- Created from libfprint %s -->\n", VERSION);
	printf ("<deviceinfo version=\"0.2\">\n");

	for (i = 0; list[i] != NULL; i++) {
		print_driver (list[i]);
	}

	printf ("</deviceinfo>\n");

	return 0;
}
