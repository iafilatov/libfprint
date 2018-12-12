/*
 * Polling/timing management
 * Copyright (C) 2008 Daniel Drake <dsd@gentoo.org>
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

#define FP_COMPONENT "poll"

#include "fp_internal.h"
#include "fpi-poll.h"

#include <config.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include <glib.h>
#include <libusb.h>

/**
 * SECTION:events
 * @title: Initialisation and events handling
 * @short_description: Initialisation and events handling functions
 *
 * These functions are only applicable to users of libfprint's asynchronous
 * API.
 *
 * libfprint does not create internal library threads and hence can only
 * execute when your application is calling a libfprint function. However,
 * libfprint often has work to be do, such as handling of completed USB
 * transfers, and processing of timeouts required in order for the library
 * to function. Therefore it is essential that your own application must
 * regularly "phone into" libfprint so that libfprint can handle any pending
 * events.
 *
 * The function you must call is fp_handle_events() or a variant of it. This
 * function will handle any pending events, and it is from this context that
 * all asynchronous event callbacks from the library will occur. You can view
 * this function as a kind of iteration function.
 *
 * If there are no events pending, fp_handle_events() will block for a few
 * seconds (and will handle any new events should anything occur in that time).
 * If you wish to customise this timeout, you can use
 * fp_handle_events_timeout() instead. If you wish to do a non-blocking
 * iteration, call fp_handle_events_timeout() with a zero timeout.
 *
 * How to integrate events handling depends on your main loop implementation.
 * The sister fprintd project includes an implementation of main loop handling
 * that integrates into GLib's main loop. The
 * [libusb documentation](http://libusb.sourceforge.net/api-1.0/group__poll.html#details)
 * also includes more details about how to integrate libfprint events into
 * your main loop.
 */

/**
 * SECTION:fpi-poll
 * @title: Timeouts
 * @short_description: Timeout handling helpers
 *
 * Helper functions to schedule a function call to be made after a timeout. This
 * is useful to avoid making blocking calls while waiting for hardware to answer
 * for example.
 */

/* this is a singly-linked list of pending timers, sorted with the timer that
 * is expiring soonest at the head. */
static GSList *active_timers = NULL;

/* notifiers for added or removed poll fds */
static fp_pollfd_added_cb fd_added_cb = NULL;
static fp_pollfd_removed_cb fd_removed_cb = NULL;

struct fpi_timeout {
	struct timeval expiry;
	fpi_timeout_fn callback;
	struct fp_dev *dev;
	void *data;
	char *name;
};

static gboolean fpi_poll_is_setup(void);

static int timeout_sort_fn(gconstpointer _a, gconstpointer _b)
{
	fpi_timeout *a = (fpi_timeout *) _a;
	fpi_timeout *b = (fpi_timeout *) _b;
	struct timeval *tv_a = &a->expiry;
	struct timeval *tv_b = &b->expiry;

	if (timercmp(tv_a, tv_b, <))
		return -1;
	else if (timercmp(tv_a, tv_b, >))
		return 1;
	else
		return 0;
}

static void
fpi_timeout_free(fpi_timeout *timeout)
{
	if (timeout == NULL)
		return;

	g_free(timeout->name);
	g_free(timeout);
}

/**
 * fpi_timeout_set_name:
 * @timeout: a #fpi_timeout
 * @name: the name to give the timeout
 *
 * Sets a name for a timeout, allowing that name to be printed
 * along with any timeout related debug.
 */
void
fpi_timeout_set_name(fpi_timeout *timeout,
		     const char  *name)
{
	g_return_if_fail (timeout != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (timeout->name == NULL);

	timeout->name = g_strdup(name);
}

/**
 * fpi_timeout_add:
 * @msec: the time before calling the function, in milliseconds (1/1000ths of a second)
 * @callback: function to callback
 * @dev: a struct #fp_dev
 * @data: data to pass to @callback, or %NULL
 *
 * A timeout is the asynchronous equivalent of sleeping. You create a timeout
 * saying that you'd like to have a function invoked at a certain time in
 * the future.
 *
 * Note that you should hold onto the return value of this function to cancel it
 * use fpi_timeout_cancel(), otherwise the callback could be called while the driver
 * is being torn down.
 *
 * This function can be considered to never fail.
 *
 * Returns: an #fpi_timeout structure
 */
fpi_timeout *fpi_timeout_add(unsigned int    msec,
			     fpi_timeout_fn  callback,
			     struct fp_dev  *dev,
			     void           *data)
{
	struct timespec ts;
	struct timeval add_msec;
	fpi_timeout *timeout;
	int r;

	g_return_val_if_fail (dev != NULL, NULL);
	g_return_val_if_fail (fpi_poll_is_setup(), NULL);

	fp_dbg("in %dms", msec);

	r = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (r < 0) {
		fp_err("failed to read monotonic clock, errno=%d", errno);
		BUG();
		return NULL;
	}

	timeout = g_new0(fpi_timeout, 1);
	timeout->callback = callback;
	timeout->dev = dev;
	timeout->data = data;
	TIMESPEC_TO_TIMEVAL(&timeout->expiry, &ts);

	/* calculate timeout expiry by adding delay to current monotonic clock */
	timerclear(&add_msec);
	add_msec.tv_sec = msec / 1000;
	add_msec.tv_usec = (msec % 1000) * 1000;
	timeradd(&timeout->expiry, &add_msec, &timeout->expiry);

	active_timers = g_slist_insert_sorted(active_timers, timeout,
		timeout_sort_fn);

	return timeout;
}

/**
 * fpi_timeout_cancel:
 * @timeout: an #fpi_timeout structure
 *
 * Cancels a timeout scheduled with fpi_timeout_add(), and frees the
 * @timeout structure.
 */
void fpi_timeout_cancel(fpi_timeout *timeout)
{
	G_DEBUG_HERE();
	active_timers = g_slist_remove(active_timers, timeout);
	fpi_timeout_free(timeout);
}

void
fpi_timeout_cancel_for_dev(struct fp_dev *dev)
{
	GSList *l;

	g_return_if_fail (dev != NULL);

	l = active_timers;
	while (l) {
		struct fpi_timeout *timeout = l->data;
		GSList *current = l;

		l = l->next;
		if (timeout->dev == dev) {
			fpi_timeout_free (timeout);
			active_timers = g_slist_delete_link (active_timers, current);
		}
	}
}

/* get the expiry time and optionally the timeout structure for the next
 * timeout. returns 0 if there are no expired timers, or 1 if the
 * timeval/timeout output parameters were populated. if the returned timeval
 * is zero then it means the timeout has already expired and should be handled
 * ASAP. */
static int get_next_timeout_expiry(struct timeval *out,
	struct fpi_timeout **out_timeout)
{
	struct timespec ts;
	struct timeval tv;
	struct fpi_timeout *next_timeout;
	int r;

	if (active_timers == NULL)
		return 0;

	r = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (r < 0) {
		fp_err("failed to read monotonic clock, errno=%d", errno);
		return r;
	}
	TIMESPEC_TO_TIMEVAL(&tv, &ts);

	next_timeout = active_timers->data;
	if (out_timeout)
		*out_timeout = next_timeout;

	if (timercmp(&tv, &next_timeout->expiry, >=)) {
		if (next_timeout->name)
			fp_dbg("first timeout '%s' already expired", next_timeout->name);
		else
			fp_dbg("first timeout already expired");
		timerclear(out);
	} else {
		timersub(&next_timeout->expiry, &tv, out);
		if (next_timeout->name)
			fp_dbg("next timeout '%s' in %ld.%06lds", next_timeout->name,
			       out->tv_sec, out->tv_usec);
		else
			fp_dbg("next timeout in %ld.%06lds", out->tv_sec, out->tv_usec);
	}

	return 1;
}

/* handle a timeout that has expired */
static void handle_timeout(struct fpi_timeout *timeout)
{
	G_DEBUG_HERE();
	timeout->callback(timeout->dev, timeout->data);
	active_timers = g_slist_remove(active_timers, timeout);
	fpi_timeout_free(timeout);
}

static int handle_timeouts(void)
{
	struct timeval next_timeout_expiry;
	struct fpi_timeout *next_timeout;
	int r;

	r = get_next_timeout_expiry(&next_timeout_expiry, &next_timeout);
	if (r <= 0)
		return r;

	if (!timerisset(&next_timeout_expiry))
		handle_timeout(next_timeout);

	return 0;
}

/**
 * fp_handle_events_timeout:
 * @timeout: Maximum timeout for this blocking function
 *
 * Handle any pending events. If a non-zero timeout is specified, the function
 * will potentially block for the specified amount of time, although it may
 * return sooner if events have been handled. The function acts as non-blocking
 * for a zero timeout.
 *
 * Returns: 0 on success, non-zero on error.
 */
API_EXPORTED int fp_handle_events_timeout(struct timeval *timeout)
{
	struct timeval next_timeout_expiry;
	struct timeval select_timeout;
	struct fpi_timeout *next_timeout;
	int r;

	r = get_next_timeout_expiry(&next_timeout_expiry, &next_timeout);
	if (r < 0)
		return r;

	if (r) {
		/* timer already expired? */
		if (!timerisset(&next_timeout_expiry)) {
			handle_timeout(next_timeout);
			return 0;
		}

		/* choose the smallest of next URB timeout or user specified timeout */
		if (timercmp(&next_timeout_expiry, timeout, <))
			select_timeout = next_timeout_expiry;
		else
			select_timeout = *timeout;
	} else {
		select_timeout = *timeout;
	}

	r = libusb_handle_events_timeout(fpi_usb_ctx, &select_timeout);
	*timeout = select_timeout;
	if (r < 0)
		return r;

	return handle_timeouts();
}

/**
 * fp_handle_events:
 *
 * Convenience function for calling fp_handle_events_timeout() with a sensible
 * default timeout value of two seconds (subject to change if we decide another
 * value is more sensible).
 *
 * Returns: 0 on success, non-zero on error.
 */
API_EXPORTED int fp_handle_events(void)
{
	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	return fp_handle_events_timeout(&tv);
}

/**
 * fp_get_next_timeout:
 * @tv: a #timeval structure containing the duration to the next timeout.
 *
 * A zero filled @tv timeout means events are to be handled immediately
 *
 * Returns: returns 0 if no timeouts active, or 1 if timeout returned.
 */
API_EXPORTED int fp_get_next_timeout(struct timeval *tv)
{
	struct timeval fprint_timeout = { 0, 0 };
	struct timeval libusb_timeout = { 0, 0 };
	int r_fprint;
	int r_libusb;

	r_fprint = get_next_timeout_expiry(&fprint_timeout, NULL);
	r_libusb = libusb_get_next_timeout(fpi_usb_ctx, &libusb_timeout);

	/* if we have no pending timeouts and the same is true for libusb,
	 * indicate that we have no pending timouts */
	if (r_fprint <= 0 && r_libusb <= 0)
		return 0;

	/* if fprint have no pending timeouts return libusb timeout */
	else if (r_fprint == 0)
		*tv = libusb_timeout;

	/* if libusb have no pending timeouts return fprint timeout */
	else if (r_libusb == 0)
		*tv = fprint_timeout;

	/* otherwise return the smaller of the 2 timeouts */
	else if (timercmp(&fprint_timeout, &libusb_timeout, <))
		*tv = fprint_timeout;
	else
		*tv = libusb_timeout;
	return 1;
}

/**
 * fp_get_pollfds:
 * @pollfds: output location for a list of pollfds. If non-%NULL, must be
 * released with free() when done.
 *
 * Retrieve a list of file descriptors that should be polled for events
 * interesting to libfprint. This function is only for users who wish to
 * combine libfprint's file descriptor set with other event sources – more
 * simplistic users will be able to call fp_handle_events() or a variant
 * directly.
 *
 * Returns: the number of pollfds in the resultant list, or negative on error.
 */
API_EXPORTED ssize_t fp_get_pollfds(struct fp_pollfd **pollfds)
{
	const struct libusb_pollfd **usbfds;
	const struct libusb_pollfd *usbfd;
	struct fp_pollfd *ret;
	ssize_t cnt = 0;
	size_t i = 0;

	g_return_val_if_fail (fpi_usb_ctx != NULL, -EIO);

	usbfds = libusb_get_pollfds(fpi_usb_ctx);
	if (!usbfds) {
		*pollfds = NULL;
		return -EIO;
	}

	while ((usbfd = usbfds[i++]) != NULL)
		cnt++;

	ret = g_malloc(sizeof(struct fp_pollfd) * cnt);
	i = 0;
	while ((usbfd = usbfds[i]) != NULL) {
		ret[i].fd = usbfd->fd;
		ret[i].events = usbfd->events;
		i++;
	}

	*pollfds = ret;
	return cnt;
}

/**
 * fp_set_pollfd_notifiers:
 * @added_cb: a #fp_pollfd_added_cb callback or %NULL
 * @removed_cb: a #fp_pollfd_removed_cb callback or %NULL
 *
 * This sets the callback functions to call for every new or removed
 * file descriptor used as an event source.
 */
API_EXPORTED void fp_set_pollfd_notifiers(fp_pollfd_added_cb added_cb,
	fp_pollfd_removed_cb removed_cb)
{
	fd_added_cb = added_cb;
	fd_removed_cb = removed_cb;
}

static void add_pollfd(int fd, short events, void *user_data)
{
	if (fd_added_cb)
		fd_added_cb(fd, events);
}

static void remove_pollfd(int fd, void *user_data)
{
	if (fd_removed_cb)
		fd_removed_cb(fd);
}

void fpi_poll_init(void)
{
	libusb_set_pollfd_notifiers(fpi_usb_ctx, add_pollfd, remove_pollfd, NULL);
}

void fpi_poll_exit(void)
{
	g_slist_free_full(active_timers, (GDestroyNotify) fpi_timeout_free);
	active_timers = NULL;
	fd_added_cb = NULL;
	fd_removed_cb = NULL;
	libusb_set_pollfd_notifiers(fpi_usb_ctx, NULL, NULL, NULL);
}

static gboolean
fpi_poll_is_setup(void)
{
	return (fd_added_cb != NULL && fd_removed_cb != NULL);
}

void
fpi_timeout_cancel_all_for_dev(struct fp_dev *dev)
{
	GSList *l;

	g_return_if_fail (dev != NULL);

	l = active_timers;
	while (l) {
		struct fpi_timeout *timeout = l->data;
		GSList *current = l;

		l = l->next;
		if (timeout->dev == dev) {
			g_free (timeout);
			active_timers = g_slist_delete_link (active_timers, current);
		}
	}
}
