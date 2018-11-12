/*
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

#ifndef __FPI_LOG_H__
#define __FPI_LOG_H__

/**
 * SECTION:fpi-log
 * @title: Logging
 * @short_description: Logging functions
 *
 * Logging in libfprint is handled through GLib's logging system, and behave the same
 * way as in the GLib [Message Output and Debugging Functions](https://developer.gnome.org/glib/stable/glib-Message-Logging.html)
 * documentation.
 *
 * You should include `fpi-log.h` as early as possible in your sources, just after
 * setting the `FP_COMPONENT` define to a string unique to your sources. This will
 * set the suffix of the `G_LOG_DOMAIN` used for printing.
 */

#ifdef FP_COMPONENT
#undef G_LOG_DOMAIN
#ifndef __GTK_DOC_IGNORE__
#define G_LOG_DOMAIN "libfprint-"FP_COMPONENT
#endif
#endif

#include <glib.h>

/**
 * fp_dbg:
 *
 * Same as g_debug().
 *
 */
#define fp_dbg g_debug

/**
 * fp_info:
 *
 * Same as g_debug().
 */
#define fp_info g_debug

/**
 * fp_warn:
 *
 * Same as g_warning().
 */
#define fp_warn g_warning

/**
 * fp_err:
 *
 * Same as g_warning(). In the future, this might be changed to a
 * g_assert() instead, so bear this in mind when adding those calls
 * to your driver.
 */
#define fp_err g_warning

/**
 * BUG_ON:
 * @condition: the condition to check
 *
 * Uses fp_err() to print an error if the @condition is true.
 */
#define BUG_ON(condition) G_STMT_START		\
	if (condition) {			\
		char *s;			\
		s = g_strconcat ("BUG: (", #condition, ")", NULL); \
		fp_err ("%s: %s() %s:%d", s, G_STRFUNC, __FILE__, __LINE__); \
		g_free (s);			\
	} G_STMT_END

/**
 * BUG:
 *
 * Same as BUG_ON() but is always true.
 */
#define BUG() BUG_ON(1)

#endif
