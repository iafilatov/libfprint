/*
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

#ifndef __FPI_USB_H__
#define __FPI_USB_H__

#include <libusb.h>
#include "fpi-dev.h"
#include "fpi-ssm.h"

/**
 * fpi_usb_transfer:
 *
 * A structure containing the arguments passed to fpi_usb_fill_bulk_transfer()
 * to be used with fpi_usb_submit_transfer().
 */
typedef struct fpi_usb_transfer fpi_usb_transfer;

/**
 * fpi_usb_transfer_cb_fn:
 * @transfer: a struct #libusb_transfer
 * @dev: the struct #fp_dev on which the operation was performed
 * @ssm: the #fpi_ssm state machine
 * @user_data: the user data passed to fpi_usb_fill_bulk_transfer()
 *
 * This callback will be called in response to a libusb bulk transfer
 * triggered via fpi_usb_fill_bulk_transfer() finishing. Note that the
 * struct #libusb_transfer does not need to be freed, as it will be
 * freed after the callback returns, similarly to
 * the [LIBUSB_TRANSFER_FREE_TRANSFER flag](http://libusb.sourceforge.net/api-1.0/group__asyncio.html#gga1fb47dd0f7c209b60a3609ff0c03d56dacf3f064997b283a14097c9f4d6f8ccc1).
 *
 * Note that the cancelled status of the transfer should be checked
 * first thing, as the @dev, @ssm and @user_data pointers might not
 * be pointing to valid values anymore. See fpi_usb_cancel_transfer()
 * for more information.
 */
typedef void(*fpi_usb_transfer_cb_fn) (struct libusb_transfer *transfer,
				       struct fp_dev          *dev,
				       fpi_ssm                *ssm,
				       void                   *user_data);

struct libusb_transfer *fpi_usb_alloc(void) __attribute__((returns_nonnull));

fpi_usb_transfer *fpi_usb_fill_bulk_transfer (struct fp_dev          *dev,
					      fpi_ssm                *ssm,
					      unsigned char           endpoint,
					      unsigned char          *buffer,
					      int                     length,
					      fpi_usb_transfer_cb_fn  callback,
					      void                   *user_data,
					      unsigned int            timeout);
int fpi_usb_submit_transfer (fpi_usb_transfer *transfer);
int fpi_usb_cancel_transfer (fpi_usb_transfer *transfer);

#endif
