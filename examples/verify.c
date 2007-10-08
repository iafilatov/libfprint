/*
 * Example fingerprint verification program
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

#include <stdio.h>
#include <stdlib.h>

#include <libfprint/fprint.h>

struct fp_dscv_dev *discover_device(struct fp_dscv_dev **discovered_devs)
{
	struct fp_dscv_dev *ddev = NULL;
	int i;

	for (i = 0; ddev = discovered_devs[i]; i++) {
		const struct fp_driver *drv = fp_dscv_dev_get_driver(ddev);
		printf("Found device claimed by %s driver\n",
			fp_driver_get_full_name(drv));
		return ddev;
	}

	return ddev;
}

struct fp_print_data *enroll(struct fp_dev *dev) {
	struct fp_print_data *enrolled_print = NULL;
	enum fp_enroll_status status;

	printf("You will need to successfully scan your finger %d times to "
		"complete the process.\n", fp_dev_get_nr_enroll_stages(dev));

	do {
		printf("Scan your finger now.\n");
		status = fp_enroll_finger(dev, &enrolled_print);
		switch (status) {
		case FP_ENROLL_COMPLETE:
			printf("Enroll complete!\n");
			break;
		case FP_ENROLL_FAIL:
			printf("Enroll failed, something wen't wrong :(\n");
			return NULL;
		case FP_ENROLL_PASS:
			printf("Enroll stage passed. Yay!\n");
			break;
		case FP_ENROLL_RETRY:
			printf("Didn't quite catch that. Please try again.\n");
			break;
		}
	} while (status != FP_ENROLL_COMPLETE);

	if (!enrolled_print) {
		fprintf(stderr, "Enroll complete but no print?\n");
		return NULL;
	}

	printf("got a print!\n");
	return enrolled_print;
}

int main(void)
{
	int r;
	int ret = 1;
	struct fp_dscv_dev *ddev;
	struct fp_dscv_dev **discovered_devs;
	struct fp_dev *dev;
	struct fp_print_data *data;

	r = fp_init();
	if (r < 0) {
		fprintf(stderr, "Failed to initialize libfprint\n");
		exit(1);
	}

	discovered_devs = fp_discover_devs();
	if (!discovered_devs) {
		fprintf(stderr, "Could not discover devices\n");
		exit(1);
	}

	ddev = discover_device(discovered_devs);
	if (!ddev) {
		fprintf(stderr, "No devices detected.\n");
		exit(1);
	}

	dev = fp_dev_open(ddev);
	fp_dscv_devs_free(discovered_devs);
	if (!dev) {
		fprintf(stderr, "Could not open device.\n");
	}

	printf("Opened device. It's now time to enroll your finger.\n\n");
	data = enroll(dev);
	if (!data)
		goto out_close;

	ret = 0;
out_close:
	fp_dev_close(dev);
	return ret;
}


