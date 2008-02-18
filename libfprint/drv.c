/*
 * Functions to assist with asynchronous driver <---> library communications
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
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

#define FP_COMPONENT "drv"

#include <errno.h>

#include "fp_internal.h"

/* Lib-driver: start device initialisation */
int fpi_drv_init(struct fp_dev *dev, unsigned long driver_data)
{
	struct fp_driver *drv = dev->drv;
	if (!drv->init) {
		fpi_drvcb_init_complete(dev, 0);
		return 0;
	}
	dev->state = DEV_STATE_INITIALIZING;
	return drv->init(dev, driver_data);
}

/* Driver-lib: device initialisation complete */
void fpi_drvcb_init_complete(struct fp_dev *dev, int status)
{
	fp_dbg("status %d", status);
	BUG_ON(dev->state != DEV_STATE_INITIALIZING);
	dev->state = (status) ? DEV_STATE_ERROR : DEV_STATE_INITIALIZED;
}

/* Lib-driver: start device deinitialisation */
void fpi_drv_deinit(struct fp_dev *dev)
{
	struct fp_driver *drv = dev->drv;
	if (!drv->deinit) {
		fpi_drvcb_deinit_complete(dev);
		return;
	}

	dev->state = DEV_STATE_DEINITIALIZING;
	drv->deinit(dev);
}

/* Driver-lib: device deinitialisation complete */
void fpi_drvcb_deinit_complete(struct fp_dev *dev)
{
	fp_dbg("");
	BUG_ON(dev->state != DEV_STATE_DEINITIALIZING);
	dev->state = DEV_STATE_DEINITIALIZED;
}

/* Lib-driver: start enrollment */
int fpi_drv_enroll_start(struct fp_dev *dev, fp_enroll_stage_cb callback)
{
	struct fp_driver *drv = dev->drv;
	int r;
	fp_dbg("");
	if (!drv->enroll_start)
		return -ENOTSUP;
	dev->state = DEV_STATE_ENROLL_STARTING;
	dev->enroll_cb = callback;
	r = drv->enroll_start(dev);
	if (r < 0) {
		dev->enroll_cb = NULL;
		dev->state = DEV_STATE_ERROR;
	}
	return r;
}

/* Driver-lib: enrollment has now started, expect results soon */
void fpi_drvcb_enroll_started(struct fp_dev *dev, int status)
{
	fp_dbg("status %d", status);
	BUG_ON(dev->state != DEV_STATE_ENROLL_STARTING);
	dev->state = (status) ? DEV_STATE_ERROR : DEV_STATE_ENROLLING;
}

/* Driver-lib: an enroll stage has completed */
void fpi_drvcb_enroll_stage_completed(struct fp_dev *dev, int result,
	struct fp_print_data *data, struct fp_img *img)
{
	BUG_ON(dev->state != DEV_STATE_ENROLLING);
	fp_dbg("result %d", result);
	if (!dev->enroll_cb) {
		fp_dbg("ignoring enroll result as no callback is subscribed");
		return;
	}
	if (result == FP_ENROLL_COMPLETE && !data) {
		fp_err("BUG: complete but no data?");
		result = FP_ENROLL_FAIL;
	}
	dev->enroll_cb(dev, result, data, img);
}

/* Lib-driver: stop enrollment */
int fpi_drv_enroll_stop(struct fp_dev *dev)
{
	struct fp_driver *drv = dev->drv;
	fp_dbg("");
	dev->enroll_cb = NULL;

	if (!drv->enroll_start)
		return -ENOTSUP;	
	if (!drv->enroll_stop) {
		dev->state = DEV_STATE_INITIALIZED;
		return 0;
	}

	dev->state = DEV_STATE_ENROLL_STOPPING;
	return drv->enroll_stop(dev);
}

/* Driver-lib: enrollment has stopped */
void fpi_drvcb_enroll_stopped(struct fp_dev *dev)
{
	fp_dbg("");
	BUG_ON(dev->state != DEV_STATE_ENROLL_STOPPING);
	dev->state = DEV_STATE_INITIALIZED;
}

/* Lib-driver: start verification */
int fpi_drv_verify_start(struct fp_dev *dev, fp_verify_cb callback,
	struct fp_print_data *data)
{
	struct fp_driver *drv = dev->drv;
	int r;

	fp_dbg("");
	if (!drv->verify_start)
		return -ENOTSUP;
	dev->state = DEV_STATE_VERIFY_STARTING;
	dev->verify_cb = callback;
	dev->verify_data = data;
	r = drv->verify_start(dev);
	if (r < 0) {
		dev->verify_cb = NULL;
		dev->state = DEV_STATE_ERROR;
	}
	return r;
}

/* Driver-lib: verification has started, expect results soon */
void fpi_drvcb_verify_started(struct fp_dev *dev, int status)
{
	fp_dbg("");
	BUG_ON(dev->state != DEV_STATE_VERIFY_STARTING);
	dev->state = (status) ? DEV_STATE_ERROR : DEV_STATE_VERIFYING;
}

/* Driver-lib: report a verify result (which might mark completion) */
void fpi_drvcb_report_verify_result(struct fp_dev *dev, int result,
	struct fp_img *img)
{
	fp_dbg("result %d", result);
	BUG_ON(dev->state != DEV_STATE_VERIFYING);
	if (result < 0 || result == FP_VERIFY_NO_MATCH
			|| result == FP_VERIFY_MATCH) {
		dev->state = DEV_STATE_VERIFY_DONE;
	}

	if (!dev->verify_cb) {
		fp_dbg("ignoring verify result as no callback is subscribed");
		return;
	}
	dev->verify_cb(dev, result, img);
}

/* Lib-driver: stop verification */
int fpi_drv_verify_stop(struct fp_dev *dev)
{
	struct fp_driver *drv = dev->drv;
	gboolean iterating = (dev->state == DEV_STATE_VERIFYING);

	fp_dbg("");
	BUG_ON(dev->state != DEV_STATE_ERROR
		&& dev->state != DEV_STATE_VERIFYING
		&& dev->state != DEV_STATE_VERIFY_DONE);
	dev->verify_cb = NULL;

	if (!drv->verify_start)
		return -ENOTSUP;	
	if (!drv->verify_stop) {
		dev->state = DEV_STATE_INITIALIZED;
		return 0;
	}

	dev->state = DEV_STATE_VERIFY_STOPPING;
	return drv->verify_stop(dev, iterating);
}

/* Driver-lib: verification has stopped */
void fpi_drvcb_verify_stopped(struct fp_dev *dev)
{
	fp_dbg("");
	BUG_ON(dev->state != DEV_STATE_VERIFY_STOPPING);
	dev->state = DEV_STATE_INITIALIZED;
}


/* Lib-driver: start identification */
int fpi_drv_identify_start(struct fp_dev *dev, fp_identify_cb callback,
	struct fp_print_data **gallery)
{
	struct fp_driver *drv = dev->drv;
	int r;

	fp_dbg("");
	if (!drv->identify_start)
		return -ENOTSUP;
	dev->state = DEV_STATE_IDENTIFY_STARTING;
	dev->identify_cb = callback;
	dev->identify_data = gallery;
	r = drv->identify_start(dev);
	if (r < 0) {
		dev->identify_cb = NULL;
		dev->state = DEV_STATE_ERROR;
	}
	return r;
}

/* Driver-lib: identification has started, expect results soon */
void fpi_drvcb_identify_started(struct fp_dev *dev, int status)
{
	fp_dbg("");
	BUG_ON(dev->state != DEV_STATE_IDENTIFY_STARTING);
	dev->state = (status) ? DEV_STATE_ERROR : DEV_STATE_IDENTIFYING;
}

/* Driver-lib: report a verify result (which might mark completion) */
void fpi_drvcb_report_identify_result(struct fp_dev *dev, int result,
	size_t match_offset, struct fp_img *img)
{
	fp_dbg("result %d", result);
	BUG_ON(dev->state != DEV_STATE_IDENTIFYING
		&& dev->state != DEV_STATE_ERROR);
	if (result < 0 || result == FP_VERIFY_NO_MATCH
			|| result == FP_VERIFY_MATCH) {
		dev->state = DEV_STATE_IDENTIFY_DONE;
	}

	if (!dev->identify_cb) {
		fp_dbg("ignoring verify result as no callback is subscribed");
		return;
	}
	dev->identify_cb(dev, result, match_offset, img);
}

/* Lib-driver: stop identification */
int fpi_drv_identify_stop(struct fp_dev *dev)
{
	struct fp_driver *drv = dev->drv;
	gboolean iterating = (dev->state == DEV_STATE_IDENTIFYING);

	fp_dbg("");
	BUG_ON(dev->state != DEV_STATE_IDENTIFYING
		&& dev->state != DEV_STATE_IDENTIFY_DONE);
	dev->identify_cb = NULL;

	if (!drv->identify_start)
		return -ENOTSUP;	
	if (!drv->identify_stop) {
		dev->state = DEV_STATE_INITIALIZED;
		return 0;
	}

	dev->state = DEV_STATE_IDENTIFY_STOPPING;
	return drv->identify_stop(dev, iterating);
}

/* Driver-lib: identification has stopped */
void fpi_drvcb_identify_stopped(struct fp_dev *dev)
{
	fp_dbg("");
	BUG_ON(dev->state != DEV_STATE_IDENTIFY_STOPPING);
	dev->state = DEV_STATE_INITIALIZED;
}

/* SSM: sequential state machine
 * Asynchronous driver design encourages some kind of state machine behind it.
 * In most cases, the state machine is entirely linear - you only go to the
 * next state, you never jump or go backwards. The SSM functions help you
 * implement such a machine.
 *
 * e.g. S1 --> S2 --> S3 --> S4
 * S1 is the start state
 * There is also an implicit error state and an implicit accepting state
 * (both with implicit edges from every state).
 *
 * You can also jump to any arbitrary state (while marking completion of the
 * current state) while the machine is running. In other words there are
 * implicit edges linking one state to every other state. OK, we're stretching
 * the "state machine" description at this point.
 *
 * To create a ssm, you pass a state handler function and the total number of
 * states (4 in the above example).
 *
 * To start a ssm, you pass in a completion callback function which gets
 * called when the ssm completes (both on error and on failure).
 *
 * To iterate to the next state, call fpi_ssm_next_state(). It is legal to
 * attempt to iterate beyond the final state - this is equivalent to marking
 * the ssm as successfully completed.
 *
 * To mark successful completion of a SSM, either iterate beyond the final
 * state or call fpi_ssm_mark_completed() from any state.
 *
 * To mark failed completion of a SSM, call fpi_ssm_mark_aborted() from any
 * state. You must pass a non-zero error code.
 *
 * Your state handling function looks at ssm->cur_state in order to determine
 * the current state and hence which operations to perform (a switch statement
 * is appropriate).
 * Typically, the state handling function fires off an asynchronous libusb
 * transfer, and the callback function iterates the machine to the next state
 * upon success (or aborts the machine on transfer failure).
 *
 * Your completion callback should examine ssm->error in order to determine
 * whether the ssm completed or failed. An error code of zero indicates
 * successful completion.
 */

/* Allocate a new ssm */
struct fpi_ssm *fpi_ssm_new(struct fp_dev *dev, ssm_handler_fn handler,
	int nr_states)
{
	struct fpi_ssm *machine;
	BUG_ON(nr_states < 1);

	machine = g_malloc0(sizeof(*machine));
	machine->handler = handler;
	machine->nr_states = nr_states;
	machine->dev = dev;
	machine->completed = TRUE;
	return machine;
}

/* Free a ssm */
void fpi_ssm_free(struct fpi_ssm *machine)
{
	if (!machine)
		return;
	g_free(machine);
}

/* Invoke the state handler */
static void __ssm_call_handler(struct fpi_ssm *machine)
{
	fp_dbg("%p entering state %d", machine, machine->cur_state);
	machine->handler(machine);
}

/* Start a ssm. You can also restart a completed or aborted ssm. */
void fpi_ssm_start(struct fpi_ssm *ssm, ssm_completed_fn callback)
{
	BUG_ON(!ssm->completed);
	ssm->callback = callback;
	ssm->cur_state = 0;
	ssm->completed = FALSE;
	ssm->error = 0;
	__ssm_call_handler(ssm);
}

static void __subsm_complete(struct fpi_ssm *ssm)
{
	struct fpi_ssm *parent = ssm->parentsm;
	BUG_ON(!parent);
	if (ssm->error)
		fpi_ssm_mark_aborted(parent, ssm->error);
	else
		fpi_ssm_next_state(parent);
	fpi_ssm_free(ssm);
}

/* start a SSM as a child of another. if the child completes successfully, the
 * parent will be advanced to the next state. if the child aborts, the parent
 * will be aborted with the same error code. the child will be automatically
 * freed upon completion/abortion. */
void fpi_ssm_start_subsm(struct fpi_ssm *parent, struct fpi_ssm *child)
{
	child->parentsm = parent;
	fpi_ssm_start(child, __subsm_complete);
}

/* Mark a ssm as completed successfully. */
void fpi_ssm_mark_completed(struct fpi_ssm *machine)
{
	BUG_ON(machine->completed);
	machine->completed = TRUE;
	fp_dbg("%p completed with status %d", machine, machine->error);
	if (machine->callback)
		machine->callback(machine);
}

/* Mark a ssm as aborted with error. */
void fpi_ssm_mark_aborted(struct fpi_ssm *machine, int error)
{
	fp_dbg("error %d from state %d", error, machine->cur_state);
	BUG_ON(error == 0);
	machine->error = error;
	fpi_ssm_mark_completed(machine);
}

/* Iterate to next state of a ssm */
void fpi_ssm_next_state(struct fpi_ssm *machine)
{
	BUG_ON(machine->completed);
	machine->cur_state++;
	if (machine->cur_state == machine->nr_states) {
		fpi_ssm_mark_completed(machine);
	} else {
		__ssm_call_handler(machine);
	}
}

void fpi_ssm_jump_to_state(struct fpi_ssm *machine, int state)
{
	BUG_ON(machine->completed);
	BUG_ON(state >= machine->nr_states);
	machine->cur_state = state;
	__ssm_call_handler(machine);
}

