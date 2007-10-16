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
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>

#include "fp_internal.h"

#define DIR_PERMS 0700

/* FIXME: should free this during library shutdown */
static char *base_store = NULL;

static void storage_setup(void)
{
	const char *homedir;

	homedir = g_getenv("HOME");
	if (!homedir)
		homedir = g_get_home_dir();
	if (!homedir)
		return;

	base_store = g_build_filename(homedir, ".fprint/prints", NULL);
	g_mkdir_with_parents(base_store, DIR_PERMS);
	/* FIXME handle failure */
}

static const char *finger_code_to_str(enum fp_finger finger)
{
	const char *names[] = {
		[LEFT_THUMB] = "lthu",
		[LEFT_INDEX] = "lind",
		[LEFT_MIDDLE] = "lmid",
		[LEFT_RING] = "lrin",
		[LEFT_LITTLE] = "llit",
		[RIGHT_THUMB] = "rthu",
		[RIGHT_INDEX] = "rind",
		[RIGHT_MIDDLE] = "rmid",
		[RIGHT_RING] = "rrin",
		[RIGHT_LITTLE] = "rlit",
	};
	if (finger < LEFT_THUMB || finger > RIGHT_LITTLE)
		return NULL;
	return names[finger];
}

struct fp_print_data *fpi_print_data_new(struct fp_dev *dev, size_t length)
{
	struct fp_print_data *data = g_malloc(sizeof(*data) + length);
	fp_dbg("length=%zd", length);
	data->driver_name = dev->drv->name;
	data->length = length;
	return data;
}

API_EXPORTED int fp_print_data_save(struct fp_print_data *data,
	enum fp_finger finger)
{
	GError *err = NULL;
	char *path;
	char *dirpath;
	const char *fingerstr = finger_code_to_str(finger);
	int r;

	if (!fingerstr)
		return -EINVAL;

	if (!base_store)
		storage_setup();

	fp_dbg("save %s print from %s", fingerstr, data->driver_name);
	dirpath = g_build_filename(base_store, data->driver_name, NULL);
	r = g_mkdir_with_parents(dirpath, DIR_PERMS);
	if (r < 0) {
		fp_err("couldn't create storage directory");
		g_free(dirpath);
		return r;
	}

	path = g_build_filename(dirpath, fingerstr, NULL);
	fp_dbg("saving to %s", path);
	g_file_set_contents(path, data->buffer, data->length, &err);
	g_free(dirpath);
	g_free(path);
	if (err) {
		r = err->code;
		fp_err("%s save failed: %s", fingerstr, err->message);
		g_error_free(err);
		return r;
	}

	return 0;
}

API_EXPORTED int fp_print_data_load(struct fp_dev *dev,
	enum fp_finger finger, struct fp_print_data **data)
{
	const char *fingerstr = finger_code_to_str(finger);
	gchar *path;
	gsize length;
	gchar *contents;
	GError *err = NULL;
	struct fp_print_data *fdata;

	if (!fingerstr)
		return -EINVAL;

	if (!base_store)
		storage_setup();

	path = g_build_filename(base_store, dev->drv->name, fingerstr, NULL);
	fp_dbg("from %s", path);
	g_file_get_contents(path, &contents, &length, &err);
	g_free(path);
	if (err) {
		int r = err->code;
		fp_err("%s load failed: %s", fingerstr, err->message);
		g_error_free(err);
		if (r == G_FILE_ERROR_NOENT)
			return -ENOENT;
		else
			return r;
	}

	fdata = fpi_print_data_new(dev, length);
	memcpy(fdata->buffer, contents, length);
	g_free(contents);
	*data = fdata;
	return 0;
}

API_EXPORTED void fp_print_data_free(struct fp_print_data *data)
{
	g_free(data);
}

int fpi_print_data_compatible(struct fp_dev *dev, struct fp_print_data *data)
{
	return strcmp(dev->drv->name, data->driver_name) == 0;
}
