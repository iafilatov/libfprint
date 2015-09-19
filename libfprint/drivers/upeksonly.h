/*
 * UPEK TouchStrip Sensor-Only driver for libfprint
 * Copyright (C) 2008 Daniel Drake <dsd@gentoo.org>
 *
 * TCS4C (USB ID 147e:1000) support:
 * Copyright (C) 2010 Hugo Grostabussiat <dw23.devel@gmail.com>
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

struct sonly_regwrite {
	uint8_t reg;
	uint8_t value;
};

/***** AWAIT FINGER *****/

static const struct sonly_regwrite awfsm_2016_writev_1[] = {
	{ 0x0a, 0x00 }, { 0x0a, 0x00 }, { 0x09, 0x20 }, { 0x03, 0x3b },
	{ 0x00, 0x67 }, { 0x00, 0x67 },
};

static const struct sonly_regwrite awfsm_1000_writev_1[] = {
	/* Initialize sensor settings */
	{ 0x0a, 0x00 }, { 0x09, 0x20 }, { 0x03, 0x37 }, { 0x00, 0x5f },
	{ 0x01, 0x6e }, { 0x01, 0xee }, { 0x0c, 0x13 }, { 0x0d, 0x0d },
	{ 0x0e, 0x0e }, { 0x0f, 0x0d },

	{ 0x13, 0x05 }, { 0x13, 0x45 },

	/* Initialize finger detection registers (not enabling yet) */
	{ 0x30, 0xe0 }, { 0x15, 0x26 },

	{ 0x12, 0x01 }, { 0x20, 0x01 }, { 0x07, 0x10 },
	{ 0x10, 0x00 }, { 0x11, 0xbf },
};

static const struct sonly_regwrite awfsm_2016_writev_2[] = {
	{ 0x01, 0xc6 }, { 0x0c, 0x13 }, { 0x0d, 0x0d }, { 0x0e, 0x0e },
	{ 0x0f, 0x0d }, { 0x0b, 0x00 },
};

static const struct sonly_regwrite awfsm_1000_writev_2[] = {
	/* Enable finger detection */
	{ 0x30, 0xe1 }, { 0x15, 0x06 }, { 0x15, 0x86 },
};

static const struct sonly_regwrite awfsm_2016_writev_3[] = {
	{ 0x13, 0x45 }, { 0x30, 0xe0 }, { 0x12, 0x01 }, { 0x20, 0x01 },
	{ 0x09, 0x20 }, { 0x0a, 0x00 }, { 0x30, 0xe0 }, { 0x20, 0x01 },
};

static const struct sonly_regwrite awfsm_2016_writev_4[] = {
	{ 0x08, 0x00 }, { 0x10, 0x00 }, { 0x12, 0x01 }, { 0x11, 0xbf },
	{ 0x12, 0x01 }, { 0x07, 0x10 }, { 0x07, 0x10 }, { 0x04, 0x00 },\
	{ 0x05, 0x00 }, { 0x0b, 0x00 },

	/* enter finger detection mode */
	{ 0x15, 0x20 }, { 0x30, 0xe1 }, { 0x15, 0x24 }, { 0x15, 0x04 },
	{ 0x15, 0x84 },
};

/***** CAPTURE MODE *****/

static const struct sonly_regwrite capsm_2016_writev[] = {
	/* enter capture mode */
	{ 0x09, 0x28 }, { 0x13, 0x55 }, { 0x0b, 0x80 }, { 0x04, 0x00 },
	{ 0x05, 0x00 },
};

static const struct sonly_regwrite capsm_1000_writev[] = {
	{ 0x08, 0x80 }, { 0x13, 0x55 }, { 0x0b, 0x80 }, /* Enter capture mode */
};

/***** DEINITIALIZATION *****/

static const struct sonly_regwrite deinitsm_2016_writev[] = {
	/* reset + enter low power mode */
	{ 0x0b, 0x00 }, { 0x09, 0x20 }, { 0x13, 0x45 }, { 0x13, 0x45 },
};

static const struct sonly_regwrite deinitsm_1000_writev[] = {
	{ 0x15, 0x26 }, { 0x30, 0xe0 }, /* Disable finger detection */

	{ 0x0b, 0x00 }, { 0x13, 0x45 }, { 0x08, 0x00 }, /* Disable capture mode */
};

/***** INITIALIZATION *****/

static const struct sonly_regwrite initsm_2016_writev_1[] = {
	{ 0x49, 0x00 },

	/* BSAPI writes different values to register 0x3e each time. I initially
	 * thought this was some kind of clever authentication, but just blasting
	 * these sniffed values each time seems to work. */
	{ 0x3e, 0x83 }, { 0x3e, 0x4f }, { 0x3e, 0x0f }, { 0x3e, 0xbf },
	{ 0x3e, 0x45 }, { 0x3e, 0x35 }, { 0x3e, 0x1c }, { 0x3e, 0xae },

	{ 0x44, 0x01 }, { 0x43, 0x06 }, { 0x43, 0x05 }, { 0x43, 0x04 },
	{ 0x44, 0x00 }, { 0x0b, 0x00 },
};

static const struct sonly_regwrite initsm_1000_writev_1[] = {
	{ 0x49, 0x00 }, /* Encryption disabled */

	/* Setting encryption key. Doesn't need to be random since we don't use any
	 * encryption. */
	{ 0x3e, 0x7f }, { 0x3e, 0x7f }, { 0x3e, 0x7f }, { 0x3e, 0x7f },
	{ 0x3e, 0x7f }, { 0x3e, 0x7f }, { 0x3e, 0x7f }, { 0x3e, 0x7f },

	{ 0x04, 0x00 }, { 0x05, 0x00 },

	{ 0x0b, 0x00 }, { 0x08, 0x00 }, /* Initialize capture control registers */
};
