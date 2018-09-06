/*
 * Logging
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

#ifdef FP_COMPONENT
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "libfprint-"FP_COMPONENT
#endif

#include <glib.h>

#define fp_dbg g_debug
#define fp_info g_debug
#define fp_warn g_warning
#define fp_err g_warning

#define BUG_ON(condition) G_STMT_START		\
	if (condition) {			\
		char *s;			\
		s = g_strconcat ("BUG: (", #condition, ")", NULL); \
		g_warning ("%s: %s() %s:%d", s, G_STRFUNC, __FILE__, __LINE__); \
		g_free (s);			\
	} G_STMT_END
#define BUG() BUG_ON(1)

#endif
