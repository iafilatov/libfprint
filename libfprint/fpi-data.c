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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "fp_internal.h"

#define DIR_PERMS 0700

struct fpi_print_data_fp2 {
	char prefix[3];
	uint16_t driver_id;
	uint32_t devtype;
	unsigned char data_type;
	unsigned char data[0];
} __attribute__((__packed__));

struct fpi_print_data_item_fp2 {
	uint32_t length;
	unsigned char data[0];
} __attribute__((__packed__));

/**
 * SECTION: print_data
 * @title: Stored prints
 * @short_description: Stored prints functions
 *
 * Stored prints are represented by a structure named #fp_print_data.
 * Stored prints are originally obtained from an enrollment function such as
 * fp_enroll_finger().
 *
 * This page documents the various operations you can do with a stored print.
 * Note that by default, "stored prints" are not actually stored anywhere
 * except in RAM. For the simple scenarios, libfprint provides a simple API
 * for you to save and load the stored prints referring to a single user in
 * their home directory. For more advanced users, libfprint provides APIs for
 * you to convert print data to a byte string, and to reconstruct stored prints
 * from such data at a later point. You are welcome to store these byte strings
 * in any fashion that suits you.
 */

/*
 * SECTION: fpi-data
 * @title: Stored prints creation
 * @short_description: Stored prints creation functions
 *
 * Stored print can be loaded and created by certain drivers which do their own
 * print matching in hardware. Most drivers will not be using those functions.
 * See #fp_print_data for the public API counterpart.
 */

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

void fpi_data_exit(void)
{
	g_free(base_store);
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
	uint32_t devtype, enum fp_print_data_type type)
{
	struct fp_print_data *data = g_malloc0(sizeof(*data));
	fp_dbg("driver=%02x devtype=%04x", driver_id, devtype);
	data->driver_id = driver_id;
	data->devtype = devtype;
	data->type = type;
	return data;
}

static void fpi_print_data_item_free(struct fp_print_data_item *item)
{
	g_free(item);
}

struct fp_print_data_item *fpi_print_data_item_new(size_t length)
{
	struct fp_print_data_item *item = g_malloc0(sizeof(*item) + length);
	item->length = length;

	return item;
}

struct fp_print_data *fpi_print_data_new(struct fp_dev *dev)
{
	return print_data_new(dev->drv->id, dev->devtype,
		fpi_driver_get_data_type(dev->drv));
}

struct fp_print_data_item *
fpi_print_data_get_item(struct fp_print_data *data)
{
	return data->prints->data;
}

void
fpi_print_data_add_item(struct fp_print_data      *data,
			struct fp_print_data_item *item)
{
	data->prints = g_slist_prepend(data->prints, item);
}

/**
 * fp_print_data_get_data:
 * @data: the stored print
 * @ret: output location for the data buffer. Must be freed with free()
 * after use.

 * Convert a stored print into a unified representation inside a data buffer.
 * You can then store this data buffer in any way that suits you, and load
 * it back at some later time using fp_print_data_from_data().
 *
 * Returns: the size of the freshly allocated buffer, or 0 on error.
 */
API_EXPORTED size_t fp_print_data_get_data(struct fp_print_data *data,
	unsigned char **ret)
{
	struct fpi_print_data_fp2 *out_data;
	struct fpi_print_data_item_fp2 *out_item;
	struct fp_print_data_item *item;
	size_t buflen = 0;
	GSList *list_item;
	unsigned char *buf;

	G_DEBUG_HERE();

	list_item = data->prints;
	while (list_item) {
		item = list_item->data;
		buflen += sizeof(*out_item);
		buflen += item->length;
		list_item = g_slist_next(list_item);
	}

	buflen += sizeof(*out_data);
	out_data = g_malloc(buflen);

	*ret = (unsigned char *) out_data;
	buf = out_data->data;
	out_data->prefix[0] = 'F';
	out_data->prefix[1] = 'P';
	out_data->prefix[2] = '2';
	out_data->driver_id = GUINT16_TO_LE(data->driver_id);
	out_data->devtype = GUINT32_TO_LE(data->devtype);
	out_data->data_type = data->type;

	list_item = data->prints;
	while (list_item) {
		item = list_item->data;
		out_item = (struct fpi_print_data_item_fp2 *)buf;
		out_item->length = GUINT32_TO_LE(item->length);
		/* FIXME: fp_print_data_item->data content is not endianess agnostic */
		memcpy(out_item->data, item->data, item->length);
		buf += sizeof(*out_item);
		buf += item->length;
		list_item = g_slist_next(list_item);
	}

	return buflen;
}

static struct fp_print_data *fpi_print_data_from_fp1_data(unsigned char *buf,
	size_t buflen)
{
	size_t print_data_len;
	struct fp_print_data *data;
	struct fp_print_data_item *item;
	struct fpi_print_data_fp2 *raw = (struct fpi_print_data_fp2 *) buf;

	print_data_len = buflen - sizeof(*raw);
	data = print_data_new(GUINT16_FROM_LE(raw->driver_id),
		GUINT32_FROM_LE(raw->devtype), raw->data_type);
	item = fpi_print_data_item_new(print_data_len);
	/* FIXME: fp_print_data->data content is not endianess agnostic */
	memcpy(item->data, raw->data, print_data_len);
	data->prints = g_slist_prepend(data->prints, item);

	return data;
}

static struct fp_print_data *fpi_print_data_from_fp2_data(unsigned char *buf,
	size_t buflen)
{
	size_t total_data_len, item_len;
	struct fp_print_data *data;
	struct fp_print_data_item *item;
	struct fpi_print_data_fp2 *raw = (struct fpi_print_data_fp2 *) buf;
	unsigned char *raw_buf;
	struct fpi_print_data_item_fp2 *raw_item;

	total_data_len = buflen - sizeof(*raw);
	data = print_data_new(GUINT16_FROM_LE(raw->driver_id),
		GUINT32_FROM_LE(raw->devtype), raw->data_type);
	raw_buf = raw->data;
	while (total_data_len) {
		if (total_data_len < sizeof(*raw_item))
			break;
		total_data_len -= sizeof(*raw_item);

		raw_item = (struct fpi_print_data_item_fp2 *)raw_buf;
		item_len = GUINT32_FROM_LE(raw_item->length);
		fp_dbg("item len %d, total_data_len %d", (int) item_len, (int) total_data_len);
		if (total_data_len < item_len) {
			fp_err("corrupted fingerprint data");
			break;
		}
		total_data_len -= item_len;

		item = fpi_print_data_item_new(item_len);
		/* FIXME: fp_print_data->data content is not endianess agnostic */
		memcpy(item->data, raw_item->data, item_len);
		data->prints = g_slist_prepend(data->prints, item);

		raw_buf += sizeof(*raw_item);
		raw_buf += item_len;
	}

	if (g_slist_length(data->prints) == 0) {
		fp_print_data_free(data);
		data = NULL;
	}

	return data;

}

/**
 * fp_print_data_from_data:
 * @buf: the data buffer
 * @buflen: the length of the buffer

 * Load a stored print from a data buffer. The contents of said buffer must
 * be the untouched contents of a buffer previously supplied to you by the
 * fp_print_data_get_data() function.
 *
 * Returns: the stored print represented by the data, or %NULL on error. Must
 * be freed with fp_print_data_free() after use.
 */
API_EXPORTED struct fp_print_data *fp_print_data_from_data(unsigned char *buf,
	size_t buflen)
{
	struct fpi_print_data_fp2 *raw = (struct fpi_print_data_fp2 *) buf;

	fp_dbg("buffer size %zd", buflen);
	if (buflen < sizeof(*raw))
		return NULL;

	if (strncmp(raw->prefix, "FP1", 3) == 0) {
		return fpi_print_data_from_fp1_data(buf, buflen);
	} else if (strncmp(raw->prefix, "FP2", 3) == 0) {
		return fpi_print_data_from_fp2_data(buf, buflen);
	} else {
		fp_dbg("bad header prefix");
	}

	return NULL;
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

/**
 * fp_print_data_save:
 * @data: the stored print to save to disk
 * @finger: the finger that this print corresponds to
 *
 * Saves a stored print to disk, assigned to a specific finger. Even though
 * you are limited to storing only the 10 human fingers, this is a
 * per-device-type limit. For example, you can store the users right index
 * finger from a DigitalPersona scanner, and you can also save the right index
 * finger from a UPEK scanner. When you later come to load the print, the right
 * one will be automatically selected.
 *
 * This function will unconditionally overwrite a fingerprint previously
 * saved for the same finger and device type. The print is saved in a hidden
 * directory beneath the current user's home directory.
 *
 * Returns: 0 on success, non-zero on error.
 */
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
		free(buf);
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
	struct fp_print_data *fdata;

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

	fdata = fp_print_data_from_data(contents, length);
	g_free(contents);
	if (!fdata)
		return -EIO;
	*data = fdata;
	return 0;
}

/**
 * fp_print_data_load:
 * @dev: the device you are loading the print for
 * @finger: the finger of the file you are loading
 * @data: output location to put the corresponding stored print. Must be
 * freed with fp_print_data_free() after use.

 * Loads a previously stored print from disk. The print must have been saved
 * earlier using the fp_print_data_save() function.
 *
 * A return code of -ENOENT indicates that the fingerprint requested could not
 * be found. Other error codes (both positive and negative) are possible for
 * obscure error conditions (e.g. corruption).
 *
 * Returns: 0 on success, non-zero on error
 */
API_EXPORTED int fp_print_data_load(struct fp_dev *dev,
	enum fp_finger finger, struct fp_print_data **data)
{
	gchar *path;
	struct fp_print_data *fdata = NULL;
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

/**
 * fp_print_data_delete:
 * @dev: the device that the print belongs to
 * @finger: the finger of the file you are deleting

 * Removes a stored print from disk previously saved with fp_print_data_save().
 *
 * Returns: 0 on success, negative on error
 */
API_EXPORTED int fp_print_data_delete(struct fp_dev *dev,
	enum fp_finger finger)
{
	int r;
	gchar *path = get_path_to_print(dev, finger);

	fp_dbg("remove finger %d at %s", finger, path);
	r = g_unlink(path);
	g_free(path);
	if (r < 0)
		fp_dbg("unlink failed with error %d", r);

	/* FIXME: cleanup empty directory */
	return r;
}

/**
 * fp_print_data_from_dscv_print:
 * @print: the discovered print
 * @data: output location to point to the corresponding stored print. Must
 * be freed with fp_print_data_free() after use.

 * Attempts to load a stored print based on a #fp_dscv_print
 * discovered print record.
 *
 * A return code of -ENOENT indicates that the file referred to by the
 * discovered print could not be found. Other error codes (both positive and
 * negative) are possible for obscure error conditions (e.g. corruption).
 *
 * Returns: 0 on success, non-zero on error.
 *
 * Deprecated: Do not use.
 */
API_EXPORTED int fp_print_data_from_dscv_print(struct fp_dscv_print *print,
	struct fp_print_data **data)
{
	return load_from_file(print->path, data);
}

/**
 * fp_print_data_free:
 * @data: the stored print to destroy. If NULL, function simply returns.
 *
 * Frees a stored print. Must be called when you are finished using the print.
 */
API_EXPORTED void fp_print_data_free(struct fp_print_data *data)
{
	if (data)
		g_slist_free_full(data->prints, (GDestroyNotify)fpi_print_data_item_free);
	g_free(data);
}

/**
 * fp_print_data_get_driver_id:
 * @data: the stored print

 * Gets the [driver ID](advanced-topics.html#driver_id) for a stored print. The driver ID
 * indicates which driver the print originally came from. The print is
 * only usable with a device controlled by that driver.
 *
 * Returns: the driver ID of the driver compatible with the print
 */
API_EXPORTED uint16_t fp_print_data_get_driver_id(struct fp_print_data *data)
{
	return data->driver_id;
}

/**
 * fp_print_data_get_devtype:
 * @data: the stored print

 * Gets the [devtype](advanced-topics.html#device-types) for a stored print. The devtype represents
 * which type of device under the parent driver is compatible with the print.
 *
 * Returns: the devtype of the device range compatible with the print
 */
API_EXPORTED uint32_t fp_print_data_get_devtype(struct fp_print_data *data)
{
	return data->devtype;
}

/**
 * SECTION:dscv_print
 * @title: Print discovery (deprecated)
 * @short_description: Print discovery functions
 *
 * The [stored print](libfprint-Stored-prints.html) documentation detailed a simple API
 * for storing per-device prints for a single user, namely
 * fp_print_data_save(). It also detailed a load function,
 * fp_print_data_load(), but usage of this function is limited to scenarios
 * where you know which device you would like to use, and you know which
 * finger you are looking to verify.
 *
 * In other cases, it would be more useful to be able to enumerate all
 * previously saved prints, potentially even before device discovery. These
 * functions are designed to offer this functionality to you.
 *
 * Discovered prints are stored in a #fp_dscv_print structure, and you
 * can use functions documented below to access some information about these
 * prints. You can determine if a discovered print appears to be compatible
 * with a device using functions such as fp_dscv_dev_supports_dscv_print() and
 * fp_dev_supports_dscv_print().
 *
 * When you are ready to use the print, you can load it into memory in the form
 * of a stored print by using the fp_print_data_from_dscv_print() function.
 *
 * You may have noticed the use of the word "appears" in the above paragraphs.
 * libfprint performs print discovery simply by examining the file and
 * directory structure of libfprint's private data store. It does not examine
 * the actual prints themselves. Just because a print has been discovered
 * and appears to be compatible with a certain device does not necessarily mean
 * that it is usable; when you come to load or use it, under unusual
 * circumstances it may turn out that the print is corrupt or not for the
 * device that it appeared to be. Also, it is possible that the print may have
 * been deleted by the time you come to load it.
 *
 * Note that this portion of the library is deprecated. All that it offers is
 * already implementable using publicly available functions, and its usage is
 * unnecessarily restrictive in terms of how it stores data.
 */

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

	while ((ent = g_dir_read_name(dir))) {
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

	while ((ent = g_dir_read_name(dir))) {
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

/**
 * fp_discover_prints:
 *
 * Scans the users home directory and returns a list of prints that were
 * previously saved using fp_print_data_save().
 *
 * Returns: a %NULL-terminated list of discovered prints, must be freed with
 * fp_dscv_prints_free() after use.
 *
 * Deprecated: Do not use.
 */
API_EXPORTED struct fp_dscv_print **fp_discover_prints(void)
{
	GDir *dir;
	const gchar *ent;
	GError *err = NULL;
	GSList *tmplist = NULL;
	GSList *elem;
	GPtrArray *array;

	if (!base_store)
		storage_setup();

	dir = g_dir_open(base_store, 0, &err);
	if (!dir) {
		fp_err("opendir %s failed: %s", base_store, err->message);
		g_error_free(err);
		return NULL;
	}

	while ((ent = g_dir_read_name(dir))) {
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

	if (tmplist == NULL)
		return NULL;

	array = g_ptr_array_new();
	for (elem = tmplist; elem != NULL; elem = elem->next)
		g_ptr_array_add(array, elem->data);
	g_ptr_array_add(array, NULL);

	g_slist_free(tmplist);
	return (struct fp_dscv_print **) g_ptr_array_free(array, FALSE);
}

/**
 * fp_dscv_prints_free:
 * @prints: the list of discovered prints. If NULL, function simply
 * returns.
 *
 * Frees a list of discovered prints. This function also frees the discovered
 * prints themselves, so make sure you do not use any discovered prints
 * after calling this function.
 *
 * Deprecated: Do not use.
 */
API_EXPORTED void fp_dscv_prints_free(struct fp_dscv_print **prints)
{
	int i;
	struct fp_dscv_print *print;

	if (!prints)
		return;

	for (i = 0; (print = prints[i]); i++) {
		if (print)
			g_free(print->path);
		g_free(print);
	}
	g_free(prints);
}

/**
 * fp_dscv_print_get_driver_id:
 * @print: the discovered print
 *
 * Gets the [driver ID](advanced-topics.html#driver_id) for a discovered print. The driver ID
 * indicates which driver the print originally came from. The print is only
 * usable with a device controlled by that driver.
 *
 * Returns: the driver ID of the driver compatible with the print
 *
 * Deprecated: Do not use.
 */
API_EXPORTED uint16_t fp_dscv_print_get_driver_id(struct fp_dscv_print *print)
{
	return print->driver_id;
}

/**
 * fp_dscv_print_get_devtype:
 * @print: the discovered print
 *
 * Gets the [devtype](advanced-topics.html#device-types) for a discovered print. The devtype
 * represents which type of device under the parent driver is compatible
 * with the print.
 *
 * Returns: the devtype of the device range compatible with the print
 *
 * Deprecated: Do not use.
 */
API_EXPORTED uint32_t fp_dscv_print_get_devtype(struct fp_dscv_print *print)
{
	return print->devtype;
}

/**
 * fp_dscv_print_get_finger:
 * @print: discovered print
 *
 * Gets the finger code for a discovered print.
 *
 * Returns: a finger code from #fp_finger
 *
 * Deprecated: Do not use.
 */
API_EXPORTED enum fp_finger fp_dscv_print_get_finger(struct fp_dscv_print *print)
{
	return print->finger;
}

/**
 * fp_dscv_print_delete:
 * @print: the discovered print to remove from disk
 *
 * Removes a discovered print from disk. After successful return of this
 * function, functions such as fp_dscv_print_get_finger() will continue to
 * operate as before, however calling fp_print_data_from_dscv_print() will
 * fail for obvious reasons.
 *
 * Returns: 0 on success, negative on error
 *
 * Deprecated: Do not use.
 */
API_EXPORTED int fp_dscv_print_delete(struct fp_dscv_print *print)
{
	int r;
	fp_dbg("remove at %s", print->path);
	r = g_unlink(print->path);
	if (r < 0)
		fp_dbg("unlink failed with error %d", r);

	/* FIXME: cleanup empty directory */
	return r;
}

