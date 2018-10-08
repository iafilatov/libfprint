/*
 * fprint D-Bus daemon
 * Copyright (C) 2008 Daniel Drake <dsd@gentoo.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <libfprint/fprint.h>

#include <poll.h>
#include <stdlib.h>

#include "loop.h"

struct fdsource {
	GSource source;
	GSList *pollfds;
};

static gboolean source_prepare(GSource *source, gint *timeout)
{
	int r;
	struct timeval tv;

	r = fp_get_next_timeout(&tv);
	if (r == 0) {
		*timeout = -1;
		return FALSE;
	}

	if (!timerisset(&tv))
		return TRUE;

	*timeout = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
	return FALSE;
}

static gboolean source_check(GSource *source)
{
	struct fdsource *_fdsource = (struct fdsource *) source;
	GSList *l;
	struct timeval tv;
	int r;

	if (!_fdsource->pollfds)
		return FALSE;

	for (l = _fdsource->pollfds; l != NULL; l = l->next) {
		GPollFD *pollfd = l->data;

		if (pollfd->revents)
			return TRUE;
	}

	r = fp_get_next_timeout(&tv);
	if (r == 1 && !timerisset(&tv))
		return TRUE;

	return FALSE;
}

static gboolean source_dispatch(GSource *source, GSourceFunc callback,
	gpointer data)
{
	struct timeval zerotimeout = {
		.tv_sec = 0,
		.tv_usec = 0,
	};

	/* FIXME error handling */
	fp_handle_events_timeout(&zerotimeout);

	/* FIXME whats the return value used for? */
	return TRUE;
}

static void source_finalize(GSource *source)
{
	struct fdsource *_fdsource = (struct fdsource *) source;
	GSList *l;

	if (!_fdsource->pollfds)
		return;

	for (l = _fdsource->pollfds; l != NULL; l = l->next) {
		GPollFD *pollfd = l->data;

		g_source_remove_poll((GSource *) _fdsource, pollfd);
		g_slice_free(GPollFD, pollfd);
		_fdsource->pollfds = g_slist_delete_link(_fdsource->pollfds, l);
	}

	g_slist_free(_fdsource->pollfds);
}

static GSourceFuncs sourcefuncs = {
	.prepare = source_prepare,
	.check = source_check,
	.dispatch = source_dispatch,
	.finalize = source_finalize,
};

static struct fdsource *fdsource = NULL;

static void pollfd_add(int fd, short events)
{
	GPollFD *pollfd;

	pollfd = g_slice_new(GPollFD);
	pollfd->fd = fd;
	pollfd->events = 0;
	pollfd->revents = 0;
	if (events & POLLIN)
		pollfd->events |= G_IO_IN;
	if (events & POLLOUT)
		pollfd->events |= G_IO_OUT;

	fdsource->pollfds = g_slist_prepend(fdsource->pollfds, pollfd);
	g_source_add_poll((GSource *) fdsource, pollfd);
}

static void pollfd_added_cb(int fd, short events)
{
	g_debug("now monitoring fd %d", fd);
	pollfd_add(fd, events);
}

static void pollfd_removed_cb(int fd)
{
	GSList *l;

	g_debug("no longer monitoring fd %d", fd);

	if (!fdsource->pollfds) {
		g_debug("cannot remove from list as list is empty?");
		return;
	}

	for (l = fdsource->pollfds; l != NULL; l = l->next) {
		GPollFD *pollfd = l->data;

		if (pollfd->fd != fd)
			continue;

		g_source_remove_poll((GSource *) fdsource, pollfd);
		g_slice_free(GPollFD, pollfd);
		fdsource->pollfds = g_slist_delete_link(fdsource->pollfds, l);
		return;
	}

	g_error("couldn't find fd %d in list\n", fd);
}

int setup_pollfds(void)
{
	ssize_t numfds;
	size_t i;
	struct fp_pollfd *fpfds;
	GSource *gsource;

	gsource = g_source_new(&sourcefuncs, sizeof(struct fdsource));
	fdsource = (struct fdsource *) gsource;
	fdsource->pollfds = NULL;

	numfds = fp_get_pollfds(&fpfds);
	if (numfds < 0) {
		if (fpfds)
			free(fpfds);
		return (int) numfds;
	} else if (numfds > 0) {
		for (i = 0; i < numfds; i++) {
			struct fp_pollfd *fpfd = &fpfds[i];
			pollfd_add(fpfd->fd, fpfd->events);
		}
	}

	free(fpfds);
	fp_set_pollfd_notifiers(pollfd_added_cb, pollfd_removed_cb);
	g_source_attach(gsource, NULL);
	return 0;
}
