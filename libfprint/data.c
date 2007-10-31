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

#define FP_FINGER_IS_VALID(finger) \
	((finger) >= LEFT_THUMB && (finger) <= RIGHT_LITTLE)

/* for debug messages only */
static const char *finger_num_to_str(enum fp_finger finger)
{
	const char *names[] = {
		[LEFT_THUMB] = "left thumb",
		[LEFT_INDEX] = "left index",
		[LEFT_MIDDLE] = "left middle",
		[LEFT_RING] = "left ring",
		[LEFT_LITTLE] = "left little",
		[RIGHT_THUMB] = "right thumb",
		[RIGHT_INDEX] = "right index",
		[RIGHT_MIDDLE] = "right middle",
		[RIGHT_RING] = "right ring",
		[RIGHT_LITTLE] = "right little",
	};
	if (!FP_FINGER_IS_VALID(finger))
		return "UNKNOWN";
	return names[finger];
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
		fpi_driver_get_data_type(dev->drv), length);
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

static char *get_path_to_storedir(uint16_t driver_id, uint32_t devtype)
{
	char idstr[5];
	char devtypestr[9];

	g_snprintf(idstr, sizeof(idstr), "%04x", driver_id);
	g_snprintf(devtypestr, sizeof(devtypestr), "%08x", devtype);

	return g_build_filename(base_store, idstr, devtypestr, NULL);
}

static char *__get_path_to_print(uint16_t driver_id, uint32_t devtype,
	enum fp_finger finger)
{
	char *dirpath;
	char *path;
	char fingername[2];

	g_snprintf(fingername, 2, "%x", finger);

	dirpath = get_path_to_storedir(driver_id, devtype);
	path = g_build_filename(dirpath, fingername, NULL);
	g_free(dirpath);
	return path;
}

static char *get_path_to_print(struct fp_dev *dev, enum fp_finger finger)
{
	return __get_path_to_print(dev->drv->id, dev->devtype, finger);
}

API_EXPORTED int fp_print_data_save(struct fp_print_data *data,
	enum fp_finger finger)
{
	GError *err = NULL;
	char *path;
	char *dirpath;
	unsigned char *buf;
	size_t len;
	int r;

	if (!base_store)
		storage_setup();

	fp_dbg("save %s print from driver %04x", finger_num_to_str(finger),
		data->driver_id);
	len = fp_print_data_get_data(data, &buf);
	if (!len)
		return -ENOMEM;

	path = __get_path_to_print(data->driver_id, data->devtype, finger);
	dirpath = g_path_get_dirname(path);
	r = g_mkdir_with_parents(dirpath, DIR_PERMS);
	if (r < 0) {
		fp_err("couldn't create storage directory");
		g_free(path);
		g_free(dirpath);
		return r;
	}

	fp_dbg("saving to %s", path);
	g_file_set_contents(path, buf, len, &err);
	free(buf);
	g_free(dirpath);
	g_free(path);
	if (err) {
		r = err->code;
		fp_err("save failed: %s", err->message);
		g_error_free(err);
		/* FIXME interpret error codes */
		return r;
	}

	return 0;
}

gboolean fpi_print_data_compatible(uint16_t driver_id1, uint32_t devtype1,
	enum fp_print_data_type type1, uint16_t driver_id2, uint32_t devtype2,
	enum fp_print_data_type type2)
{
	if (driver_id1 != driver_id2) {
		fp_dbg("driver ID mismatch: %02x vs %02x", driver_id1, driver_id2);
		return FALSE;
	}

	if (devtype1 != devtype2) {
		fp_dbg("devtype mismatch: %04x vs %04x", devtype1, devtype2);
		return FALSE;
	}

	if (type1 != type2) {
		fp_dbg("type mismatch: %d vs %d", type1, type2);
		return FALSE;
	}

	return TRUE;
}

static int load_from_file(char *path, struct fp_print_data **data)
{
	gsize length;
	gchar *contents;
	GError *err = NULL;

	fp_dbg("from %s", path);
	g_file_get_contents(path, &contents, &length, &err);
	if (err) {
		int r = err->code;
		fp_err("%s load failed: %s", path, err->message);
		g_error_free(err);
		/* FIXME interpret more error codes */
		if (r == G_FILE_ERROR_NOENT)
			return -ENOENT;
		else
			return r;
	}

	*data = fp_print_data_from_data(contents, length);
	g_free(contents);
	return 0;
}

API_EXPORTED int fp_print_data_load(struct fp_dev *dev,
	enum fp_finger finger, struct fp_print_data **data)
{
	gchar *path;
	struct fp_print_data *fdata;
	int r;

	if (!base_store)
		storage_setup();

	path = get_path_to_print(dev, finger);
	r = load_from_file(path, &fdata);
	g_free(path);
	if (r)
		return r;

	if (!fp_dev_supports_print_data(dev, fdata)) {
		fp_err("print data is not compatible!");
		fp_print_data_free(fdata);
		return -EINVAL;
	}

	*data = fdata;
	return 0;
}

API_EXPORTED int fp_print_data_from_dscv_print(struct fp_dscv_print *print,
	struct fp_print_data **data)
{
	return load_from_file(print->path, data);
}

API_EXPORTED void fp_print_data_free(struct fp_print_data *data)
{
	g_free(data);
}

API_EXPORTED uint16_t fp_print_data_get_driver_id(struct fp_print_data *data)
{
	return data->driver_id;
}

API_EXPORTED uint32_t fp_print_data_get_devtype(struct fp_print_data *data)
{
	return data->devtype;
}

static GSList *scan_dev_store_dir(char *devpath, uint16_t driver_id,
	uint32_t devtype, GSList *list)
{
	GError *err = NULL;
	const gchar *ent;
	struct fp_dscv_print *print;

	GDir *dir = g_dir_open(devpath, 0, &err);
	if (!dir) {
		fp_err("opendir %s failed: %s", devpath, err->message);
		g_error_free(err);
		return list;
	}

	while (ent = g_dir_read_name(dir)) {
		/* ent is an 1 hex character fp_finger code */
		guint64 val;
		enum fp_finger finger;
		gchar *endptr;

		if (*ent == 0 || strlen(ent) != 1)
			continue;

		val = g_ascii_strtoull(ent, &endptr, 16);
		if (endptr == ent || !FP_FINGER_IS_VALID(val)) {
			fp_dbg("skipping print file %s", ent);
			continue;
		}

		finger = (enum fp_finger) val;
		print = g_malloc(sizeof(*print));
		print->driver_id = driver_id;
		print->devtype = devtype;
		print->path = g_build_filename(devpath, ent, NULL);
		print->finger = finger;
		list = g_slist_prepend(list, print);
	}

	g_dir_close(dir);
	return list;
}

static GSList *scan_driver_store_dir(char *drvpath, uint16_t driver_id,
	GSList *list)
{
	GError *err = NULL;
	const gchar *ent;

	GDir *dir = g_dir_open(drvpath, 0, &err);
	if (!dir) {
		fp_err("opendir %s failed: %s", drvpath, err->message);
		g_error_free(err);
		return list;
	}

	while (ent = g_dir_read_name(dir)) {
		/* ent is an 8 hex character devtype */
		guint64 val;
		uint32_t devtype;
		gchar *endptr;
		gchar *path;

		if (*ent == 0 || strlen(ent) != 8)
			continue;

		val = g_ascii_strtoull(ent, &endptr, 16);
		if (endptr == ent) {
			fp_dbg("skipping devtype %s", ent);
			continue;
		}

		devtype = (uint32_t) val;
		path = g_build_filename(drvpath, ent, NULL);
		list = scan_dev_store_dir(path, driver_id, devtype, list);
		g_free(path);
	}

	g_dir_close(dir);
	return list;
}

API_EXPORTED struct fp_dscv_print **fp_discover_prints(void)
{
	GDir *dir;
	const gchar *ent;
	GError *err = NULL;
	GSList *tmplist = NULL;
	GSList *elem;
	unsigned int tmplist_len;
	struct fp_dscv_print **list;
	unsigned int i;

	if (!base_store)
		storage_setup();

	dir = g_dir_open(base_store, 0, &err);
	if (!dir) {
		fp_err("opendir %s failed: %s", base_store, err->message);
		g_error_free(err);
		return NULL;
	}

	while (ent = g_dir_read_name(dir)) {
		/* ent is a 4 hex digit driver_id */
		gchar *endptr;
		gchar *path;
		guint64 val;
		uint16_t driver_id;

		if (*ent == 0 || strlen(ent) != 4)
			continue;

		val = g_ascii_strtoull(ent, &endptr, 16);
		if (endptr == ent) {
			fp_dbg("skipping drv id %s", ent);
			continue;
		}

		driver_id = (uint16_t) val;
		path = g_build_filename(base_store, ent, NULL);
		tmplist = scan_driver_store_dir(path, driver_id, tmplist);
		g_free(path);
	}

	g_dir_close(dir);
	tmplist_len = g_slist_length(tmplist);
	list = g_malloc(sizeof(*list) * (tmplist_len + 1));
	elem = tmplist;
	for (i = 0; i < tmplist_len; i++, elem = g_slist_next(elem))
		list[i] = elem->data;
	list[tmplist_len] = NULL; /* NULL-terminate */

	g_slist_free(tmplist);
	return list;
}

API_EXPORTED void fp_dscv_prints_free(struct fp_dscv_print **prints)
{
	int i;
	struct fp_dscv_print *print;

	if (!prints)
		return;

	for (i = 0; print = prints[i]; i++) {
		if (print)
			g_free(print->path);
		g_free(print);
	}
	g_free(prints);
}

API_EXPORTED uint16_t fp_dscv_print_get_driver_id(struct fp_dscv_print *print)
{
	return print->driver_id;
}

API_EXPORTED uint32_t fp_dscv_print_get_devtype(struct fp_dscv_print *print)
{
	return print->devtype;
}

API_EXPORTED enum fp_finger fp_dscv_print_get_finger(struct fp_dscv_print *print)
{
	return print->finger;
}
