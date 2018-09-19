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

/**
 * SECTION:fpi-usb
 * @title: Helpers for libusb
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
