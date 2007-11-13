/*
 * UPEK TouchStrip driver for libfprint
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
 *
 * Based in part on libthinkfinger:
 * Copyright (C) 2006-2007 Timo Hoenig <thoenig@suse.de>
 * Copyright (C) 2006 Pavel Machek <pavel@suse.cz>
 *
 * LGPL CRC code copied from GStreamer-0.10.10:
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2004,2006 Thomas Vander Stichele <thomas at apestaart dot org>

 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; version
 * 2.1 of the License.
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

#define FP_COMPONENT "upekts"

#include <errno.h>
#include <string.h>

#include <glib.h>
#include <usb.h>

#include <fp_internal.h>

#define EP_IN (1 | USB_ENDPOINT_IN)
#define EP_OUT (2 | USB_ENDPOINT_OUT)
#define TIMEOUT 5000

struct upekts_dev {
	uint8_t seq;
};

static const uint16_t crc_table[256] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
	0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
	0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
	0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
	0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
	0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
	0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
	0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
	0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
	0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
	0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
	0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
	0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
	0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
	0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
	0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
	0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
	0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
	0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
	0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
	0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
	0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

static uint16_t udf_crc(unsigned char *buffer, size_t size)
{
	uint16_t crc = 0;
	while (size--)
    	crc = (uint16_t) ((crc << 8) ^
			crc_table[((crc >> 8) & 0x00ff) ^ *buffer++]);
	return crc;
}

/*
 * MESSAGE FORMAT
 * 
 * Messages to and from the device have the same format.
 *
 * Byte-wise:
 * 		'C' 'i' 'a' 'o' A B L <DATA> C1 C2
 *
 * Ciao prefixes all messages. The rightmost 4 bits of B become the uppermost
 * 4 bits of L, and when combined with the lower 8 bits listed as 'L', L is
 * the length of the data, <DATA> is L bytes long. C1 and C2 are the
 * UDF-CRC16 for the whole message minus the Ciao prefix.
 *
 * When the device wants to command the driver to do something, it sends
 * a message where B=0 and A!=0. The A value indicates the type of command.
 * If the system is expected to respond to the command, it sends a message back
 * with B=0 and A incremented.
 *
 * When the driver sends a command to the device, A=0 and B is used as a
 * sequence counter. It starts at 0, increments by 0x10 on each command, and 
 * wraps around.
 * After each command is sent, the device responds with another message
 * indicating completion of the command including any data that was requested.
 * This message has the same A and B values.
 *
 * When the driver is sending commands as above, and when the device is
 * responding, the <DATA> seems to follow this structure:
 *
 * 		28 L1 L2 0 0 S <INNERDATA>
 *
 * Where the length of <INNERDATA> is L-3, and S is some kind of subcommand
 * code. L1 is the least significant bits of L, L2 is the most significant. In
 * the device's response to a command, the subcommand code will be unchanged.
 *
 * After deducing and documenting the above, I found a few places where the
 * above doesn't hold true. Those are marked with FIXME's below.
 */

#define CMD_SEQ_INCREMENT 0x10

static int send_cmd(struct fp_dev *dev, unsigned char seq_a,
	unsigned char seq_b, unsigned char *data, uint16_t len)
{
	int r;
	uint16_t crc;

	/* 9 bytes extra for: 4 byte 'Ciao', 1 byte A, 1 byte B | lenHI,
	 * 1 byte lenLO, 2 byte CRC */
	size_t urblen = len + 9;
	unsigned char *buf;

	if (!data && len > 0) {
		fp_err("len>0 but no data?");
		return -EINVAL;
	}
	
	buf = g_malloc(urblen);

	/* Write header */
	strncpy(buf, "Ciao", 4);
	len = GUINT16_TO_LE(len);
	buf[4] = seq_a;
	buf[5] = seq_b | ((len & 0xf00) >> 8);
	buf[6] = len & 0x00ff;

	/* Copy data */
	if (data)
		memcpy(buf + 7, data, len);

	/* Append CRC */
	crc = GUINT16_TO_BE(udf_crc(buf + 4, urblen - 6));
	buf[urblen - 2] = crc >> 8;
	buf[urblen - 1] = crc & 0xff;

	r = usb_bulk_write(dev->udev, EP_OUT, buf, urblen, TIMEOUT);
	g_free(buf);
	if (r < 0) {
		fp_err("cmd write failed, code %d", r);
		return r;
	} else if ((unsigned int) r < urblen) {
		fp_err("cmd write too short (%d/%d)", r, urblen);
		return -EIO;
	}

	return 0;
}

static int send_cmd28(struct fp_dev *dev, unsigned char subcmd,
	unsigned char *data, uint16_t innerlen)
{
	uint16_t _innerlen = innerlen;
	size_t len = innerlen + 6;
	unsigned char *buf = g_malloc0(len);
	struct upekts_dev *upekdev = dev->priv;
	uint8_t seq = upekdev->seq + CMD_SEQ_INCREMENT;
	int r;

	fp_dbg("seq=%02x subcmd=%02x with %d bytes of data", seq, subcmd, innerlen);

	_innerlen = GUINT16_TO_LE(innerlen + 3);
	buf[0] = 0x28;
	buf[1] = _innerlen & 0x00ff;
	buf[2] = (_innerlen & 0xff00) >> 8;
	buf[5] = subcmd;
	memcpy(buf + 6, data, innerlen);

	r = send_cmd(dev, 0, seq, buf, len);
	if (r == 0)
		upekdev->seq = seq;

	g_free(buf);
	return r;
}

static int send_cmdresponse(struct fp_dev *dev, unsigned char seq,
	unsigned char *data, uint8_t len)
{
	fp_dbg("seq=%02x len=%d", seq, len);
	return send_cmd(dev, seq, 0, data, len);
}

static unsigned char *__read_msg(struct fp_dev *dev, size_t *data_len)
{
#define MSG_READ_BUF_SIZE 0x40
#define MAX_DATA_IN_READ_BUF (MSG_READ_BUF_SIZE - 9)
	unsigned char *buf = g_malloc(MSG_READ_BUF_SIZE);
	size_t buf_size = MSG_READ_BUF_SIZE;
	uint16_t computed_crc, msg_crc;
	uint16_t len;
	int r;

	r = usb_bulk_read(dev->udev, EP_IN, buf, buf_size, TIMEOUT);
	if (r < 0) {
		fp_err("msg read failed, code %d", r);
		goto err;
	} else if (r < 9) {
		fp_err("msg read too short (%d/%d)", r, buf_size);
		goto err;
	}

	if (strncmp(buf, "Ciao", 4) != 0) {
		fp_err("no Ciao for you!!");
		goto err;
	}

	len = GUINT16_FROM_LE(((buf[5] & 0xf) << 8) | buf[6]);

	if (r != MSG_READ_BUF_SIZE && (len + 9) < r) {
		/* Check that the length claimed inside the message is in line with
		 * the amount of data that was transferred over USB. */
		fp_err("msg didn't include enough data, expected=%d recv=%d",
			len + 9, r);
		goto err;
	}

	/* We use a 64 byte buffer for reading messages. However, sometimes
	 * messages are longer, in which case we have to do another USB bulk read
	 * to read the remainder. This is handled below. */
	if (len > MAX_DATA_IN_READ_BUF) {
		int needed = len - MAX_DATA_IN_READ_BUF;
		fp_dbg("didn't fit in buffer, need to extend by %d bytes", needed);
		buf = g_realloc((gpointer) buf, MSG_READ_BUF_SIZE + needed);
		r = usb_bulk_read(dev->udev, EP_IN, buf + MSG_READ_BUF_SIZE, needed,
			TIMEOUT);
		if (r < 0) {
			fp_err("extended msg read failed, code %d", r);
			goto err;
		} else if (r < needed) {
			fp_err("extended msg short read (%d/%d)", r, needed);
			goto err;
		}
		buf_size += needed;
	}

	computed_crc = udf_crc(buf + 4, len + 3);
	msg_crc = GUINT16_FROM_LE((buf[len + 8] << 8) | buf[len + 7]);
	if (computed_crc != msg_crc) {
		fp_err("CRC failed, got %04x expected %04x", msg_crc, computed_crc);
		goto err;
	}

	*data_len = buf_size;
	return buf;
err:
	g_free(buf);
	return NULL;
}

enum read_msg_status {
	READ_MSG_ERROR = -1,
	READ_MSG_CMD = 1,
	READ_MSG_RESPONSE = 2,
};

static enum read_msg_status read_msg(struct fp_dev *dev, uint8_t *seq,
	unsigned char *subcmd, unsigned char **data, size_t *data_len)
{
#define MSG_READ_BUF_SIZE 0x40
#define MAX_DATA_IN_READ_BUF (MSG_READ_BUF_SIZE - 9)
	unsigned char *buf;
	size_t buf_size;
	unsigned char code_a;
	unsigned char code_b;
	uint16_t len;
	enum read_msg_status ret = READ_MSG_ERROR;

retry:
	buf = __read_msg(dev, &buf_size);
	if (!buf)
		return READ_MSG_ERROR;

	code_a = buf[4];
	code_b = buf[5] & 0xf0;
	len = GUINT16_FROM_LE(((buf[5] & 0xf) << 8) | buf[6]);
	fp_dbg("A=%02x B=%02x len=%d", code_a, code_b, len);

	if (code_a && !code_b) {
		/* device sends command to driver */
		fp_dbg("cmd %x from device to driver", code_a);

		if (code_a == 0x08) {
			fp_dbg("device busy, send busy-ack");
			send_cmdresponse(dev, 0x09, NULL, 0);
			g_free(buf);
			goto retry;
		}

		if (seq)
			*seq = code_a;
		if (data) {
			if (len > 0) {
				unsigned char *tmp = g_malloc(len);
				memcpy(tmp, buf + 7, len);
				*data = tmp;
			}
			*data_len = len;
		}
		ret = READ_MSG_CMD;
	} else if (!code_a) {
		/* device sends response to a previously executed command */
		unsigned char *innerbuf = buf + 7;
		unsigned char _subcmd;
		uint16_t innerlen;

		if (len < 6) {
			fp_err("cmd response too short (%d)", len);
			goto out;
		}
		if (innerbuf[0] != 0x28) {
			fp_err("cmd response without 28 byte?");
			goto out;
		}
		if (innerbuf[3] || innerbuf[4]) {
			fp_err("non-zero bytes in cmd response");
			goto out;
		}

		innerlen = innerbuf[1] | (innerbuf[2] << 8);
		innerlen = GUINT16_FROM_LE(innerlen) - 3;
		_subcmd = innerbuf[5];
		fp_dbg("device responds to subcmd %x with %d bytes", _subcmd, innerlen);
		if (seq)
			*seq = code_b;
		if (subcmd)
			*subcmd = _subcmd;
		if (data) {
			if (innerlen > 0) {
				unsigned char *tmp = g_malloc(innerlen);
				memcpy(tmp, innerbuf + 6, innerlen);
				*data = tmp;
			}
			*data_len = innerlen;
		}
		ret = READ_MSG_RESPONSE;
	} else {
		fp_err("don't know how to handle this message");
	}

out:
	g_free(buf);
	return ret;
}

static int read_msg28(struct fp_dev *dev, unsigned char subcmd,
	unsigned char **data, size_t *data_len)
{
	struct upekts_dev *upekdev = dev->priv;
	uint8_t _seq;
	unsigned char _subcmd;
	enum read_msg_status msgstat;

	msgstat = read_msg(dev, &_seq, &_subcmd, data, data_len);
	if (msgstat != READ_MSG_RESPONSE) {
		fp_err("expected response, got %d seq=%x", msgstat, _seq);
		return -EPROTO;
	}
	if (_subcmd != subcmd) {
		fp_warn("expected response to subcmd %02x, got response to %02x",
			subcmd, _subcmd);
		return -EPROTO;
	}
	if (_seq != upekdev->seq) {
		fp_err("expected response to cmd seq=%02x, got response to %02x",
			upekdev->seq, _seq);
		return -EPROTO;
	}

	return 0;
}

static const unsigned char init_resp03[] = {
	0x01, 0x00, 0xe8, 0x03, 0x00, 0x00, 0xff, 0x07
};
static const unsigned char init28_08[] = {
	0x04, 0x83, 0x00, 0x2c, 0x22, 0x23, 0x97, 0xc9, 0xa7, 0x15, 0xa0, 0x8a,
	0xab, 0x3c, 0xd0, 0xbf, 0xdb, 0xf3, 0x92, 0x6f, 0xae, 0x3b, 0x1e, 0x44,
	0xc4
};
static const unsigned char init28_0c[] = {
	0x04, 0x03, 0x00, 0x00, 0x00
};
static const unsigned char init28_0b[] = {
	0x04, 0x03, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0x64, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x0a,
	0x00, 0x64, 0x00, 0xf4, 0x01, 0x32, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00
};

static int do_init(struct fp_dev *dev)
{
	enum read_msg_status msgstat;
	unsigned char dummy = 0x10;
	uint8_t seq;
	int r;

	r = usb_control_msg(dev->udev, USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0x0c, 0x100, 0x400, &dummy, sizeof(dummy), TIMEOUT);
	if (r < 0) {
		fp_dbg("control write failed\n");
		return r;
	}

	msgstat = read_msg(dev, &seq, NULL, NULL, NULL);
	if (msgstat != READ_MSG_CMD) {
		fp_err("expected command, got %d seq=%x", msgstat, seq);
		return -EPROTO;
	}
	if (seq != 3) {
		fp_err("expected seq=3, got %x", seq);
		return -EPROTO;
	}

	r = send_cmdresponse(dev, ++seq, (unsigned char *) init_resp03,
		sizeof(init_resp03));
	if (r < 0)
		return r;
	
	msgstat = read_msg(dev, &seq, NULL, NULL, NULL);
	if (msgstat != READ_MSG_CMD) {
		fp_err("expected command, got %d seq=%x", msgstat, seq);
		return -EPROTO;
	}
	if (seq != 5) {
		fp_err("expected seq=5, got %x", seq);
		return -EPROTO;
	}

	dummy = 0x04;
	r = send_cmd28(dev, 0x06, &dummy, 1);
	if (r < 0)
		return r;
	if (read_msg28(dev, 0x06, NULL, NULL) < 0)
		return r;

	dummy = 0x04;
	r = send_cmd28(dev, 0x07, &dummy, 1);
	if (r < 0)
		return r;
	if (read_msg28(dev, 0x07, NULL, NULL) < 0)
		return r;

	r = send_cmd28(dev, 0x08, (unsigned char *) init28_08,
		sizeof(init28_08));
	if (r < 0)
		return r;
	if (read_msg28(dev, 0x08, NULL, NULL) < 0)
		return r;

	r = send_cmd28(dev, 0x0c, (unsigned char *) init28_0c,
		sizeof(init28_0c));
	if (r < 0)
		return r;
	if (read_msg28(dev, 0x0c, NULL, NULL) < 0)
		return r;

	r = send_cmd28(dev, 0x0b, (unsigned char *) init28_0b,
		sizeof(init28_0b));
	if (r < 0)
		return r;
	if (read_msg28(dev, 0x0b, NULL, NULL) < 0)
		return r;

	return 0;
}

static int do_deinit(struct fp_dev *dev)
{
	unsigned char dummy = 0;
	enum read_msg_status msgstat;
	uint8_t seq;
	int r;

	/* FIXME: either i've misunderstood the message system or this is illegal
	 * here, since we arent responding to anything. */
	r = send_cmdresponse(dev, 0x07, &dummy, 1);
	if (r < 0)
		return r;
	
	msgstat = read_msg(dev, &seq, NULL, NULL, NULL);
	if (msgstat != READ_MSG_CMD) {
		fp_err("expected command, got %d seq=%x", msgstat, seq);
		return -EPROTO;
	}
	if (seq != 1) {
		fp_err("expected seq=1, got %x", seq);
		return -EPROTO;
	}

	return 0;
}

static int dev_init(struct fp_dev *dev, unsigned long driver_data)
{
	struct upekts_dev *upekdev = NULL;
	int r;

	r = usb_claim_interface(dev->udev, 0);
	if (r < 0)
		return r;

	upekdev = g_malloc(sizeof(*upekdev));
	upekdev->seq = 0xf0; /* incremented to 0x00 before first cmd */
	dev->priv = upekdev;
	dev->nr_enroll_stages = 3;

	return 0;
}

static void dev_exit(struct fp_dev *dev)
{
	usb_release_interface(dev->udev, 0);
	g_free(dev->priv);
}

static const unsigned char enroll_init[] = {
	0x02, 0xc0, 0xd4, 0x01, 0x00, 0x04, 0x00, 0x08
};
static const unsigned char scan_comp[] = {
	0x12, 0xff, 0xff, 0xff, 0xff /* scan completion, prefixes print data */
};

/* used for enrollment and verification */
static const unsigned char poll_data[] = { 0x30, 0x01 };

static int enroll(struct fp_dev *dev, gboolean initial,
	int stage, struct fp_print_data **_data, struct fp_img **img)
{
	unsigned char *data;
	size_t data_len;
	int r;
	int result = 0;
	int passed = 0;

	if (initial) {
		r = do_init(dev);
		if (r < 0)
			return r;

		r = send_cmd28(dev, 0x02, (unsigned char *) enroll_init,
			sizeof(enroll_init));
		if (r < 0)
			return r;
		/* FIXME: protocol misunderstanding here. device receives response
		 * to subcmd 0 after submitting subcmd 2? */
		/* actually this is probably a poll response? does the above cmd
		 * include a 30 01 poll somewhere? */
		if (read_msg28(dev, 0x00, NULL, NULL) < 0)
			return -EPROTO;
	}

	while (!result) {
		unsigned char status;

		r = send_cmd28(dev, 0x00, (unsigned char *) poll_data,
			sizeof(poll_data));
		if (r < 0)
			return r;
		if (read_msg28(dev, 0x00, &data, &data_len) < 0)
			return -EPROTO;

		if (data_len != 14) {
			fp_err("received 3001 poll response of %d bytes?", data_len);
			g_free(data);
			return -EPROTO;
		}

		status = data[5];
		fp_dbg("poll result = %02x", status);

		/* These codes indicate that we're waiting for a finger scan, so poll
		 * again */
		switch (status) {
		case 0x0c:
		case 0x0d:
		case 0x0e:
			/* no news, poll again */
			if (passed)
				result = FP_ENROLL_PASS;
			break;
		case 0x1c: /* FIXME what does this one mean? */
		case 0x0b: /* FIXME what does this one mean? */
		case 0x23: /* FIXME what does this one mean? */
			result = FP_ENROLL_RETRY;
			break;
		case 0x0f: /* scan taking too long, remove finger and try again */
			result = FP_ENROLL_RETRY_REMOVE_FINGER;
			break;
		case 0x1e: /* swipe too short */
			result = FP_ENROLL_RETRY_TOO_SHORT;
			break;
		case 0x24: /* finger not centered */
			result = FP_ENROLL_RETRY_CENTER_FINGER;
			break;
		case 0x20:
			/* finger scanned successfully */
			/* don't break out immediately, need to look at the next
			 * value to determine if enrollment is complete or not */
			passed = 1;
			break;
		case 0x00:
			if (passed)
				result = FP_ENROLL_COMPLETE;
			break;
		default:
			fp_err("unrecognised scan status code %02x", status);
			result = -EPROTO;
			break;
		}
		g_free(data);
	}

	/* FIXME: need to extend protocol research to handle the case when
	 * enrolment fails, e.g. you scan a different finger on each stage */

	if (result == FP_ENROLL_COMPLETE) {
		struct fp_print_data *fdata;

		r = send_cmd28(dev, 0x00, (unsigned char *) poll_data,
			sizeof(poll_data));
		if (r < 0)
			return r;
		/* FIXME: protocol misunderstanding here. device receives response
		 * to subcmd 0 after submitting subcmd 2? */
		if (read_msg28(dev, 0x02, &data, &data_len) < 0)
			return -EPROTO;

		if (data_len < sizeof(scan_comp)) {
			fp_err("fingerprint data too short (%d bytes)", data_len);
			result = -EPROTO;
			goto comp_out;
		}
		if (memcmp(data, scan_comp, sizeof(scan_comp)) != 0) {
			fp_err("unrecognised data prefix %x %x %x %x %x",
				data[0], data[1], data[2], data[3], data[4]);
			result = -EPROTO;
			goto comp_out;
		}
		if (!_data) {
			fp_err("complete but no data storage!");
			result = FP_ENROLL_COMPLETE;
			goto comp_out;
		}

		fdata = fpi_print_data_new(dev, data_len - sizeof(scan_comp));
		memcpy(fdata->data, data + sizeof(scan_comp), data_len - sizeof(scan_comp));
		*_data = fdata;
comp_out:
		do_deinit(dev);
		g_free(data);
	}

	return result;
}

static const unsigned char verify_hdr[] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xc0, 0xd4, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
	0x00
};

static int verify(struct fp_dev *dev, struct fp_print_data *print,
	struct fp_img **img)
{
	size_t data_len = sizeof(verify_hdr) + print->length;
	unsigned char *data;
	int r;
	unsigned char status;
	gboolean need_poll = FALSE;
	gboolean done = FALSE;

	r = do_init(dev);
	if (r < 0)
		return r;

	data = g_malloc(data_len);
	memcpy(data, verify_hdr, sizeof(verify_hdr));
	memcpy(data + sizeof(verify_hdr), print->data, print->length);

	r = send_cmd28(dev, 0x03, data, data_len);
	if (r < 0)
		return r;
	g_free(data);

	while (!done) {
		if (need_poll) {
			r = send_cmd28(dev, 0x00, (unsigned char *) poll_data,
				sizeof(poll_data));
			if (r < 0)
				return r;
		} else {
			need_poll = TRUE;
		}
		if (read_msg28(dev, 0x00, &data, &data_len) < 0)
			return -EPROTO;

		if (data_len != 14) {
			fp_err("received 3001 poll response of %d bytes?", data_len);
			r = -EPROTO;
			goto out;
		}

		status = data[5];
		fp_dbg("poll result = %02x", status);

		/* These codes indicate that we're waiting for a finger scan, so poll
		 * again */
		switch (status) {
		case 0x0c: /* no news, poll again */
			break;
		case 0x20:
			fp_dbg("processing scan for verification");
			break;
		case 0x00:
			fp_dbg("good image");
			done = TRUE;
			break;
		case 0x1c: /* FIXME what does this one mean? */
		case 0x0b: /* FIXME what does this one mean? */
		case 0x23: /* FIXME what does this one mean? */
			r = FP_VERIFY_RETRY;
			goto out;
		case 0x0f: /* scan taking too long, remove finger and try again */
			r = FP_VERIFY_RETRY_REMOVE_FINGER;
			goto out;
		case 0x1e: /* swipe too short */
			r = FP_VERIFY_RETRY_TOO_SHORT;
			goto out;
		case 0x24: /* finger not centered */
			r = FP_VERIFY_RETRY_CENTER_FINGER;
			goto out;
		default:
			fp_err("unrecognised verify status code %02x", status);
			r = -EPROTO;
			goto out;
		}
		g_free(data);
	}

	if (status == 0x00) {
		/* poll again for verify result */
		r = send_cmd28(dev, 0x00, (unsigned char *) poll_data,
			sizeof(poll_data));
		if (r < 0)
			return r;
		if (read_msg28(dev, 0x03, &data, &data_len) < 0)
			return -EPROTO;
		if (data_len < 2) {
			fp_err("verify result abnormally short!");
			r = -EPROTO;
			goto out;
		}
		if (data[0] != 0x12) {
			fp_err("unexpected verify header byte %02x", data[0]);
			r = -EPROTO;
			goto out;
		}
		if (data[1] == 0x00) {
			r = FP_VERIFY_NO_MATCH;
		} else if (data[1] == 0x01) {
			r = FP_VERIFY_MATCH;
		} else {
			fp_err("unrecognised verify result %02x", data[1]);
			r = -EPROTO;
			goto out;
		}
	}

out:
	do_deinit(dev);
	g_free(data);
	return r;
}

static const struct usb_id id_table[] = {
	{ .vendor = 0x0483, .product = 0x2016 },
	{ 0, 0, 0, }, /* terminating entry */
};

struct fp_driver upekts_driver = {
	.id = 1,
	.name = FP_COMPONENT,
	.full_name = "UPEK TouchStrip",
	.id_table = id_table,
	.init = dev_init,
	.exit = dev_exit,
	.enroll = enroll,
	.verify = verify,
};

