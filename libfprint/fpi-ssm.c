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

#include "fp_internal.h"
#include "fpi-ssm.h"

#include <config.h>
#include <errno.h>

/**
 * SECTION:fpi-ssm
 * @title: Sequential state machine
 * @short_description: State machine helpers
 *
 * Asynchronous driver design encourages some kind of state machine behind it.
 * In most cases, the state machine is entirely linear - you only go to the
 * next state, you never jump or go backwards. The #fpi_ssm functions help you
 * implement such a machine.
 *
 * e.g. `S1` ↦ `S2` ↦ `S3` ↦ `S4`
 *
 * `S1` is the start state
 * There is also an implicit error state and an implicit accepting state
 * (both with implicit edges from every state).
 *
 * You can also jump to any arbitrary state (while marking completion of the
 * current state) while the machine is running. In other words there are
 * implicit edges linking one state to every other state.
 *
 * To create an #fpi_ssm, you pass a state handler function and the total number of
 * states (4 in the above example) to fpi_ssm_new(). Note that the state numbers
 * start at zero, making them match the first value in a C enumeration.
 *
 * To start a ssm, you pass in a completion callback function to fpi_ssm_start()
 * which gets called when the ssm completes (both on error and on failure).
 *
 * To iterate to the next state, call fpi_ssm_next_state(). It is legal to
 * attempt to iterate beyond the final state - this is equivalent to marking
 * the ssm as successfully completed.
 *
 * To mark successful completion of a SSM, either iterate beyond the final
 * state or call fpi_ssm_mark_completed() from any state.
 *
 * To mark failed completion of a SSM, call fpi_ssm_mark_failed() from any
 * state. You must pass a non-zero error code.
 *
 * Your state handling function looks at the return value of
 * fpi_ssm_get_cur_state() in order to determine the current state and hence
 * which operations to perform (a switch statement is appropriate).
 *
 * Typically, the state handling function fires off an asynchronous
 * communication with the device (such as a libsub transfer), and the
 * callback function iterates the machine to the next state
 * upon success (or fails).
 *
 * Your completion callback should examine the return value of
 * fpi_ssm_get_error() in order to determine whether the #fpi_ssm completed or
 * failed. An error code of zero indicates successful completion.
 */

struct fpi_ssm {
	struct fp_dev *dev;
	fpi_ssm *parentsm;
	void *user_data;
	int nr_states;
	int cur_state;
	gboolean completed;
	int error;
	ssm_completed_fn callback;
	ssm_handler_fn handler;
};

/**
 * fpi_ssm_new:
 * @dev: a #fp_dev fingerprint device
 * @handler: the callback function
 * @nr_states: the number of states
 * @user_data: the user data to pass to callbacks
 *
 * Allocate a new ssm, with @nr_states states. The @handler callback
 * will be called after each state transition.
 *
 * Returns: a new #fpi_ssm state machine
 */
fpi_ssm *fpi_ssm_new(struct fp_dev  *dev,
		     ssm_handler_fn  handler,
		     int             nr_states,
		     void           *user_data)
{
	fpi_ssm *machine;
	BUG_ON(nr_states < 1);

	machine = g_malloc0(sizeof(*machine));
	machine->handler = handler;
	machine->nr_states = nr_states;
	machine->dev = dev;
	machine->completed = TRUE;
	machine->user_data = user_data;
	return machine;
}

/**
 * fpi_ssm_get_user_data:
 * @machine: an #fpi_ssm state machine
 *
 * Retrieve the pointer to user data set when fpi_ssm_new()
 * is called.
 *
 * Returns: a pointer
 */
void *
fpi_ssm_get_user_data(fpi_ssm *machine)
{
	return machine->user_data;
}

/**
 * fpi_ssm_free:
 * @machine: an #fpi_ssm state machine
 *
 * Frees a state machine. This does not call any error or success
 * callbacks, so you need to do this yourself.
 */
void fpi_ssm_free(fpi_ssm *machine)
{
	if (!machine)
		return;
	g_free(machine);
}

/* Invoke the state handler */
static void __ssm_call_handler(fpi_ssm *machine)
{
	fp_dbg("%p entering state %d", machine, machine->cur_state);
	machine->handler(machine, machine->dev, machine->user_data);
}

/**
 * fpi_ssm_start:
 * @ssm: an #fpi_ssm state machine
 * @callback: the #ssm_completed_fn callback to call on completion
 *
 * Starts a state machine. You can also use this function to restart
 * a completed or failed state machine. The @callback will be called
 * on completion.
 */
void fpi_ssm_start(fpi_ssm *ssm, ssm_completed_fn callback)
{
	BUG_ON(!ssm->completed);
	ssm->callback = callback;
	ssm->cur_state = 0;
	ssm->completed = FALSE;
	ssm->error = 0;
	__ssm_call_handler(ssm);
}

static void __subsm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	fpi_ssm *parent = ssm->parentsm;
	BUG_ON(!parent);
	if (ssm->error)
		fpi_ssm_mark_failed(parent, ssm->error);
	else
		fpi_ssm_next_state(parent);
	fpi_ssm_free(ssm);
}

/**
 * fpi_ssm_start_subsm:
 * @parent: an #fpi_ssm state machine
 * @child: an #fpi_ssm state machine
 *
 * Starts a state machine as a child of another. if the child completes
 * successfully, the parent will be advanced to the next state. if the
 * child fails, the parent will be marked as failed with the same error code.
 *
 * The child will be automatically freed upon completion or failure.
 */
void fpi_ssm_start_subsm(fpi_ssm *parent, fpi_ssm *child)
{
	child->parentsm = parent;
	fpi_ssm_start(child, __subsm_complete);
}

/**
 * fpi_ssm_mark_completed:
 * @machine: an #fpi_ssm state machine
 *
 * Mark a ssm as completed successfully. The callback set when creating
 * the state machine with fpi_ssm_new() will be called synchronously.
 */
void fpi_ssm_mark_completed(fpi_ssm *machine)
{
	BUG_ON(machine->completed);
	machine->completed = TRUE;
	fp_dbg("%p completed with status %d", machine, machine->error);
	if (machine->callback)
		machine->callback(machine, machine->dev, machine->user_data);
}

/**
 * fpi_ssm_mark_failed:
 * @machine: an #fpi_ssm state machine
 * @error: the error code
 *
 * Mark a state machine as failed with @error as the error code.
 */
void fpi_ssm_mark_failed(fpi_ssm *machine, int error)
{
	fp_dbg("error %d from state %d", error, machine->cur_state);
	BUG_ON(error == 0);
	machine->error = error;
	fpi_ssm_mark_completed(machine);
}

/**
 * fpi_ssm_next_state:
 * @machine: an #fpi_ssm state machine
 *
 * Iterate to next state of a state machine. If the current state is the
 * last state, then the state machine will be marked as completed, as
 * if calling fpi_ssm_mark_completed().
 */
void fpi_ssm_next_state(fpi_ssm *machine)
{
	g_return_if_fail (machine != NULL);

	BUG_ON(machine->completed);
	machine->cur_state++;
	if (machine->cur_state == machine->nr_states) {
		fpi_ssm_mark_completed(machine);
	} else {
		__ssm_call_handler(machine);
	}
}

/**
 * fpi_ssm_next_state_timeout_cb:
 * @dev: a struct #fp_dev
 * @data: a pointer to an #fpi_ssm state machine
 *
 * Same as fpi_ssm_next_state(), but to be used as a callback
 * for an fpi_timeout_add() callback, when the state change needs
 * to happen after a timeout.
 *
 * Make sure to pass the #fpi_ssm as the `user_data` argument
 * for that fpi_timeout_add() call.
 */
void
fpi_ssm_next_state_timeout_cb(struct fp_dev *dev,
			      void          *data)
{
	g_return_if_fail (dev != NULL);
	g_return_if_fail (data != NULL);

	fpi_ssm_next_state(data);
}

/**
 * fpi_ssm_jump_to_state:
 * @machine: an #fpi_ssm state machine
 * @state: the state to jump to
 *
 * Jump to the @state state, bypassing intermediary states.
 */
void fpi_ssm_jump_to_state(fpi_ssm *machine, int state)
{
	BUG_ON(machine->completed);
	BUG_ON(state >= machine->nr_states);
	machine->cur_state = state;
	__ssm_call_handler(machine);
}

/**
 * fpi_ssm_get_cur_state:
 * @machine: an #fpi_ssm state machine
 *
 * Returns the value of the current state. Note that states are
 * 0-indexed, so a value of 0 means “the first state”.
 *
 * Returns: the current state.
 */
int fpi_ssm_get_cur_state(fpi_ssm *machine)
{
	return machine->cur_state;
}

/**
 * fpi_ssm_get_error:
 * @machine: an #fpi_ssm state machine
 *
 * Returns the error code set by fpi_ssm_mark_failed().
 *
 * Returns: a error code
 */
int fpi_ssm_get_error(fpi_ssm *machine)
{
	return machine->error;
}
