/*
 * Example libfprint image capture program
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
		struct fp_driver *drv = fp_dscv_dev_get_driver(ddev);
		printf("Found device claimed by %s driver\n",
			fp_driver_get_full_name(drv));
		return ddev;
	}

	return ddev;
}

int main(void)
{
	int r = 1;
	struct fp_dscv_dev *ddev;
	struct fp_dscv_dev **discovered_devs;
	struct fp_dev *dev;
	struct fp_img_dev *imgdev;
	struct fp_img *img = NULL;

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
		exit(1);
	}

	imgdev = fp_dev_to_img_dev(dev);
	if (!imgdev) {
		fprintf(stderr, "could not get image dev, is this an imaging "
			"device?\n");
		goto out_close;
	}

	printf("Opened device. It's now time to scan your finger.\n\n");

	r = fp_imgdev_capture(imgdev, 0, &img);
	if (r) {
		fprintf(stderr, "image capture failed, code %d\n", r);
		goto out_close;
	}

	r = fp_img_save_to_file(img, "finger.pgm");
	if (r) {
		fprintf(stderr, "img save failed, code %d\n", r);
		goto out_close;
	}

	fp_img_standardize(img);
	r = fp_img_save_to_file(img, "finger_standardized.pgm");
	if (r) {
		fprintf(stderr, "standardized img save failed, code %d\n", r);
		goto out_close;
	}

	r = 0;
out_close:
	fp_dev_close(dev);
	return r;
}

