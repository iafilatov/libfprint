/*
 * Driver API definitions
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
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

#ifndef __FPI_SSM_H__
#define __FPI_SSM_H__

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <libusb.h>

/* async drv <--> lib comms */

typedef struct fpi_ssm fpi_ssm;
typedef void (*ssm_completed_fn)(fpi_ssm *ssm);
typedef void (*ssm_handler_fn)(fpi_ssm *ssm);

/* sequential state machine: state machine that iterates sequentially over
 * a predefined series of states. can be aborted by either completion or
 * abortion error conditions. */

/* for library and drivers */
fpi_ssm *fpi_ssm_new(struct fp_dev *dev, ssm_handler_fn handler,
	int nr_states);
void fpi_ssm_free(fpi_ssm *machine);
void fpi_ssm_start(fpi_ssm *machine, ssm_completed_fn callback);
void fpi_ssm_start_subsm(fpi_ssm *parent, fpi_ssm *child);

/* for drivers */
void fpi_ssm_next_state(fpi_ssm *machine);
void fpi_ssm_jump_to_state(fpi_ssm *machine, int state);
void fpi_ssm_mark_completed(fpi_ssm *machine);
void fpi_ssm_mark_aborted(fpi_ssm *machine, int error);
struct fp_dev *fpi_ssm_get_dev(fpi_ssm *machine);
void fpi_ssm_set_user_data(fpi_ssm *machine,
	void *user_data);
void *fpi_ssm_get_user_data(fpi_ssm *machine);
int fpi_ssm_get_error(fpi_ssm *machine);
int fpi_ssm_get_cur_state(fpi_ssm *machine);

#endif
