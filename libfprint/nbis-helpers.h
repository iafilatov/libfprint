/*
 * Helpers to use within the NBIS copy/paste library
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

#include <glib.h>

#define ASSERT_SIZE_MUL(a,b)					\
	{							\
		gsize dest;					\
		g_assert(g_size_checked_mul(&dest, a, b));	\
	}

#define ASSERT_INT_MUL(a, b)					\
	{							\
		gsize dest;					\
		g_assert(g_size_checked_mul(&dest, a, b));	\
		g_assert(dest < G_MAXINT);			\
	}
