/*
 * Driver API definitions
 * Copyright (C) 2018 Bastien Nocera <hadess@hadess.net>
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

#include "fpi-usb.h"
#include "drivers_api.h"

/**
 * SECTION:fpi-usb
 * @title: Helpers for libusb
 * @short_description: libusb-related helpers
 *
 * A collection of [libusb helpers](http://libusb.sourceforge.net/api-1.0/group__poll.html#details)
 * to make driver development easier. Please refer to the libusb API documentation for more
 * information about the original API.
 */

/* Helpers from glib */
#include <glib.h>
#include <glib/gprintf.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* special helpers to avoid gmessage.c dependency */
static void mem_error (const char *format, ...) G_GNUC_PRINTF (1,2);
#define mem_assert(cond)    do { if (G_LIKELY (cond)) ; else mem_error ("assertion failed: %s", #cond); } while (0)

static void
mem_error (const char *format,
           ...)
{
  const char *pname;
  va_list args;
  /* at least, put out "MEMORY-ERROR", in case we segfault during the rest of the function */
  fputs ("\n***MEMORY-ERROR***: ", stderr);
  pname = g_get_prgname();
  g_fprintf (stderr, "%s[%ld]: ", pname ? pname : "", (long)getpid());
  va_start (args, format);
  g_vfprintf (stderr, format, args);
  va_end (args);
  fputs ("\n", stderr);
  abort();
  _exit (1);
}

struct fpi_usb_transfer {
	struct libusb_transfer *transfer;
	fpi_ssm *ssm;
	struct fp_dev *dev;
	fpi_usb_transfer_cb_fn callback;
	void *user_data;
};

/**
 * fpi_usb_alloc:
 *
 * Returns a struct libusb_transfer, similar to calling
 * `libusb_alloc_transfer(0)`[[1](http://libusb.sourceforge.net/api-1.0/group__asyncio.html#ga13cc69ea40c702181c430c950121c000)]. As libfprint uses GLib internally,
 * and [memory allocation failures will make applications fail](https://developer.gnome.org/glib/stable/glib-Memory-Allocation.html#glib-Memory-Allocation.description),
 * this helper will assert when the libusb call fails.
 */
struct libusb_transfer *
fpi_usb_alloc(void)
{
	struct libusb_transfer *transfer;

	transfer = libusb_alloc_transfer(0);
	mem_assert(transfer);

	return transfer;
}

static fpi_usb_transfer *
fpi_usb_transfer_new(struct fp_dev          *dev,
		     fpi_ssm                *ssm,
		     fpi_usb_transfer_cb_fn  callback,
		     void                   *user_data)
{
	fpi_usb_transfer *transfer;

	transfer = g_new0(fpi_usb_transfer, 1);
	transfer->transfer = fpi_usb_alloc();
	transfer->dev = dev;
	transfer->ssm = ssm;
	transfer->callback = callback;
	transfer->user_data = user_data;

	return transfer;
}

void
fpi_usb_transfer_free(fpi_usb_transfer *transfer)
{
	if (transfer == NULL)
		return;

	g_free(transfer->transfer->buffer);
	libusb_free_transfer(transfer->transfer);
	g_free(transfer);
}

static void
fpi_usb_transfer_cb (struct libusb_transfer *transfer)
{
	fpi_usb_transfer *t;

	g_assert(transfer);
	g_assert(transfer->user_data);

	t = transfer->user_data;
	BUG_ON(transfer->callback == NULL);
	(t->callback) (transfer, t->dev, t->ssm, t->user_data);
	fpi_usb_transfer_free(t);
}

/**
 * fpi_usb_fill_bulk_transfer:
 * @dev: a struct #fp_dev fingerprint device
 * @ssm: the current #fpi_ssm state machine
 * @endpoint: the USB end point
 * @buffer: a buffer allocated with g_malloc() or another GLib function.
 * Note that the returned #fpi_usb_transfer will own this buffer, so it
 * should not be freed manually.
 * @length: the size of @buffer
 * @callback: the callback function that will be called once the fpi_usb_submit_transfer()
 * call finishes.
 * @user_data: a user data pointer to pass to the callback
 * @timeout: timeout for the transfer in milliseconds, or 0 for no timeout
 *
 * This function is similar to calling [`libusb_alloc_transfer(0)`](http://libusb.sourceforge.net/api-1.0/group__asyncio.html#ga13cc69ea40c702181c430c950121c000)]
 * followed by calling [`libusb_fill_bulk_transfer()`](http://libusb.sourceforge.net/api-1.0/group__asyncio.html#gad4ddb1a5c6c7fefc979a44d7300b95d7).
 * The #fpi_usb_transfer_cb_fn callback will however provide more arguments
 * relevant to libfprint drivers, making it a good replacement for the raw libusb
 * calls.
 *
 * Returns: a #fpi_usb_transfer transfer struct, to be passed to
 * fpi_usb_submit_transfer().
 */
fpi_usb_transfer *
fpi_usb_fill_bulk_transfer (struct fp_dev          *dev,
			    fpi_ssm                *ssm,
			    unsigned char           endpoint,
			    unsigned char          *buffer,
			    int                     length,
			    fpi_usb_transfer_cb_fn  callback,
			    void                   *user_data,
			    unsigned int            timeout)
{
	fpi_usb_transfer *transfer;

	g_return_val_if_fail (dev != NULL, NULL);
	g_return_val_if_fail (callback != NULL, NULL);

	transfer = fpi_usb_transfer_new(dev,
					ssm,
					callback,
					user_data);

	libusb_fill_bulk_transfer(transfer->transfer,
				  fpi_dev_get_usb_dev(dev),
				  endpoint,
				  buffer,
				  length,
				  fpi_usb_transfer_cb,
				  transfer,
				  timeout);

	return transfer;
}

/**
 * fpi_usb_submit_transfer:
 * @transfer: a #fpi_usb_transfer struct
 *
 * Start a transfer to the device with the provided #fpi_usb_transfer.
 * On error, the #fpi_usb_transfer struct will be freed, otherwise it will
 * be freed once the callback provided to fpi_usb_fill_bulk_transfer() has
 * been called.
 *
 * Returns: 0 on success, or the same errors as [libusb_submit_transfer](http://libusb.sourceforge.net/api-1.0/group__asyncio.html#gabb0932601f2c7dad2fee4b27962848ce)
 * on failure.
 */
int
fpi_usb_submit_transfer(fpi_usb_transfer *transfer)
{
	int r;

	g_return_val_if_fail (transfer != NULL, LIBUSB_ERROR_INVALID_PARAM);

	r = libusb_submit_transfer(transfer->transfer);
	if (r < 0)
		fpi_usb_transfer_free(transfer);

	return r;
}

/**
 * fpi_usb_cancel_transfer:
 * @transfer: a #fpi_usb_transfer struct
 *
 * Cancel a transfer to the device with the provided #fpi_usb_transfer.
 * Note that this will not complete the cancellation, as your transfer
 * callback will be called with the `LIBUSB_TRANSFER_CANCELLED` status,
 * as [libusb_cancel_transfer](http://libusb.sourceforge.net/api-1.0/group__asyncio.html#ga685eb7731f9a0593f75beb99727bbe54)
 * would.
 *
 * You should not access anything but the given struct #libusb_transfer
 * in the callback before checking whether `LIBUSB_TRANSFER_CANCELLED` has
 * been called, as that might cause memory access violations.
 *
 * Returns: 0 on success, or the same errors as [libusb_cancel_transfer](http://libusb.sourceforge.net/api-1.0/group__asyncio.html#ga685eb7731f9a0593f75beb99727bbe54)
 * on failure.
 */
int
fpi_usb_cancel_transfer(fpi_usb_transfer *transfer)
{
	g_return_val_if_fail (transfer != NULL, LIBUSB_ERROR_NOT_FOUND);

	return libusb_cancel_transfer(transfer->transfer);
}
