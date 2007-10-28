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

static enum fp_print_data_type get_data_type_for_dev(struct fp_dev *dev)
{
	switch (dev->drv->type) {
	case DRIVER_PRIMITIVE:
		return PRINT_DATA_RAW;
	case DRIVER_IMAGING:
		return PRINT_DATA_NBIS_MINUTIAE;
	default:
		fp_err("unrecognised drv type %d", dev->drv->type);
		return PRINT_DATA_RAW;
	}
}

static struct fp_print_data *print_data_new(uint16_t driver_id,
	uint32_t devtype, enum fp_print_data_type type, size_t length)
{
	struct fp_print_data *data = g_malloc(sizeof(*data) + length);
	fp_dbg("length=%zd driver=%02x devtype=%04x", length, driver_id, devtype);
	memset(data, 0, sizeof(*data));
	data->driver_id = driver_id;
	data->devtype = devtype;
	data->type = type;
	data->length = length;
	return data;
}

struct fp_print_data *fpi_print_data_new(struct fp_dev *dev, size_t length)
{
	struct fp_print_data *data = g_malloc(sizeof(*data) + length);
	memset(data, 0, sizeof(*data));
	return print_data_new(dev->drv->id, dev->devtype,
		get_data_type_for_dev(dev), length);
}

API_EXPORTED size_t fp_print_data_get_data(struct fp_print_data *data,
	unsigned char **ret)
{
	struct fpi_print_data_fp1 *buf;
	size_t buflen;

	fp_dbg("");

	buflen = sizeof(*buf) + data->length;
	buf = malloc(buflen);
	if (!buf)
		return 0;

	*ret = (unsigned char *) buf;
	buf->prefix[0] = 'F';
	buf->prefix[1] = 'P';
	buf->prefix[2] = '1';
	buf->driver_id = GUINT16_TO_LE(data->driver_id);
	buf->devtype = GUINT32_TO_LE(data->devtype);
	buf->data_type = data->type;
	memcpy(buf->data, data->data, data->length);
	return buflen;
}

API_EXPORTED struct fp_print_data *fp_print_data_from_data(unsigned char *buf,
	size_t buflen)
{
	struct fpi_print_data_fp1 *raw = (struct fpi_print_data_fp1 *) buf;
	size_t print_data_len;
	struct fp_print_data *data;

	fp_dbg("buffer size %zd", buflen);
	if (buflen < sizeof(*raw))
		return NULL;

	if (strncmp(raw->prefix, "FP1", 3) != 0) {
		fp_dbg("bad header prefix");
		return NULL;
	}

	print_data_len = buflen - sizeof(*raw);
	data = print_data_new(GUINT16_FROM_LE(raw->driver_id),
		GUINT32_FROM_LE(raw->devtype), raw->data_type, print_data_len);
	memcpy(data->data, raw->data, print_data_len);
	return data;
}

static char *__get_path_to_storedir(uint16_t driver_id, uint32_t devtype)
{
	char idstr[5];
	char devtypestr[9];

	g_snprintf(idstr, sizeof(idstr), "%04x", driver_id);
	g_snprintf(devtypestr, sizeof(devtypestr), "%08x", devtype);

	return g_build_filename(base_store, idstr, devtypestr, NULL);
}

static char *get_path_to_storedir(struct fp_dev *dev)
{
	return __get_path_to_storedir(dev->drv->id, dev->devtype);
}

static char *get_path_to_print(struct fp_dev *dev, const char *fingerstr)
{
	char *dirpath;
	char *path;

	dirpath = get_path_to_storedir(dev);
	path = g_build_filename(dirpath, fingerstr, NULL);
	g_free(dirpath);
	return path;
}

API_EXPORTED int fp_print_data_save(struct fp_print_data *data,
	enum fp_finger finger)
{
	GError *err = NULL;
	char *path;
	char *dirpath;
	const char *fingerstr = finger_code_to_str(finger);
	unsigned char *buf;
	size_t len;
	int r;

	if (!fingerstr)
		return -EINVAL;

	if (!base_store)
		storage_setup();

	fp_dbg("save %s print from driver %04x", fingerstr, data->driver_id);
	len = fp_print_data_get_data(data, &buf);
	if (!len)
		return -ENOMEM;

	dirpath = __get_path_to_storedir(data->driver_id, data->devtype);
	r = g_mkdir_with_parents(dirpath, DIR_PERMS);
	if (r < 0) {
		fp_err("couldn't create storage directory");
		g_free(dirpath);
		return r;
	}

	path = g_build_filename(dirpath, fingerstr, NULL);
	fp_dbg("saving to %s", path);
	g_file_set_contents(path, buf, len, &err);
	free(buf);
	g_free(dirpath);
	g_free(path);
	if (err) {
		r = err->code;
		fp_err("%s save failed: %s", fingerstr, err->message);
		g_error_free(err);
		/* FIXME interpret error codes */
		return r;
	}

	return 0;
}

gboolean fpi_print_data_compatible(struct fp_print_data *data,
	struct fp_dev *dev)
{
	struct fp_driver *drv = dev->drv;

	if (drv->id != data->driver_id) {
		fp_dbg("driver name mismatch: %02x vs %02x", drv->id, data->driver_id);
		return FALSE;
	}

	if (dev->devtype != data->devtype) {
		fp_dbg("devtype mismatch: %04x vs %04x", dev->devtype, data->devtype);
		return FALSE;
	}

	switch (data->type) {
	case PRINT_DATA_RAW:
		if (drv->type != DRIVER_PRIMITIVE) {
			fp_dbg("raw data vs primitive driver mismatch");
			return FALSE;
		}
		break;
	case PRINT_DATA_NBIS_MINUTIAE:
		if (drv->type != DRIVER_IMAGING) {
			fp_dbg("minutiae data vs imaging driver mismatch");
			return FALSE;
		}
		break;
	default:
		fp_err("unrecognised data type %d", data->type);
		return FALSE;
	}

	return TRUE;
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

	path = get_path_to_print(dev, fingerstr);
	fp_dbg("from %s", path);
	g_file_get_contents(path, &contents, &length, &err);
	g_free(path);
	if (err) {
		int r = err->code;
		fp_err("%s load failed: %s", fingerstr, err->message);
		g_error_free(err);
		/* FIXME interpret more error codes */
		if (r == G_FILE_ERROR_NOENT)
			return -ENOENT;
		else
			return r;
	}

	fdata = fp_print_data_from_data(contents, length);
	g_free(contents);

	if (!fpi_print_data_compatible(fdata, dev)) {
		fp_err("print data is not compatible!");
		return -EINVAL;
	}

	*data = fdata;
	return 0;
}

API_EXPORTED void fp_print_data_free(struct fp_print_data *data)
{
	g_free(data);
}

