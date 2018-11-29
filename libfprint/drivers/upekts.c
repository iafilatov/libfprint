/*
 * UPEK TouchStrip driver for libfprint
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
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

#include "drivers_api.h"
#include "fpi-async.h"
#include "upek_proto.h"

#define EP_IN (1 | LIBUSB_ENDPOINT_IN)
#define EP_OUT (2 | LIBUSB_ENDPOINT_OUT)
#define TIMEOUT 5000

#define MSG_READ_BUF_SIZE 0x40
#define MAX_DATA_IN_READ_BUF (MSG_READ_BUF_SIZE - 9)

struct upekts_dev {
	gboolean enroll_passed;
	gboolean first_verify_iteration;
	gboolean stop_verify;
	uint8_t seq; /* FIXME: improve/automate seq handling */
};



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

static struct libusb_transfer *alloc_send_cmd_transfer(struct fp_dev *dev,
	unsigned char seq_a, unsigned char seq_b, const unsigned char *data,
	uint16_t len, libusb_transfer_cb_fn callback, void *user_data)
{
	struct libusb_transfer *transfer = fpi_usb_alloc();
	uint16_t crc;
	const char *ciao = "Ciao";

	/* 9 bytes extra for: 4 byte 'Ciao', 1 byte A, 1 byte B | lenHI,
	 * 1 byte lenLO, 2 byte CRC */
	size_t urblen = len + 9;
	unsigned char *buf;

	if (!data && len > 0) {
		fp_err("len>0 but no data?");
		return NULL;
	}

	buf = g_malloc(urblen);

	/* Write header */
	memcpy(buf, ciao, strlen(ciao));
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

	libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(dev), EP_OUT, buf, urblen,
		callback, user_data, TIMEOUT);
	return transfer;
}

static struct libusb_transfer *alloc_send_cmd28_transfer(struct fp_dev *dev,
	unsigned char subcmd, const unsigned char *data, uint16_t innerlen,
	libusb_transfer_cb_fn callback, void *user_data)
{
	uint16_t _innerlen = innerlen;
	size_t len = innerlen + 6;
	unsigned char *buf = g_malloc0(len);
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);
	uint8_t seq = upekdev->seq + CMD_SEQ_INCREMENT;
	struct libusb_transfer *ret;

	fp_dbg("seq=%02x subcmd=%02x with %d bytes of data", seq, subcmd, innerlen);

	_innerlen = GUINT16_TO_LE(innerlen + 3);
	buf[0] = 0x28;
	buf[1] = _innerlen & 0x00ff;
	buf[2] = (_innerlen & 0xff00) >> 8;
	buf[5] = subcmd;
	memcpy(buf + 6, data, innerlen);

	ret = alloc_send_cmd_transfer(dev, 0, seq, buf, len, callback, user_data);
	upekdev->seq = seq;

	g_free(buf);
	return ret;
}

static struct libusb_transfer *alloc_send_cmdresponse_transfer(
	struct fp_dev *dev, unsigned char seq, const unsigned char *data,
	uint8_t len, libusb_transfer_cb_fn callback, void *user_data)
{
	fp_dbg("seq=%02x len=%d", seq, len);
	return alloc_send_cmd_transfer(dev, seq, 0, data, len, callback, user_data);
}

enum read_msg_status {
	READ_MSG_ERROR,
	READ_MSG_CMD,
	READ_MSG_RESPONSE,
};

typedef void (*read_msg_cb_fn)(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data);

struct read_msg_data {
	struct fp_dev *dev;
	read_msg_cb_fn callback;
	void *user_data;
};

static int __read_msg_async(struct read_msg_data *udata);

#define READ_MSG_DATA_CB_ERR(udata) (udata)->callback((udata)->dev, \
	READ_MSG_ERROR, 0, 0, NULL, 0, (udata)->user_data)

static void busy_ack_sent_cb(struct libusb_transfer *transfer)
{
	struct read_msg_data *udata = transfer->user_data;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED ||
			transfer->length != transfer->actual_length) {
		READ_MSG_DATA_CB_ERR(udata);
		g_free(udata);
	} else {
		int r = __read_msg_async(udata);
		if (r < 0) {
			READ_MSG_DATA_CB_ERR(udata);
			g_free(udata);
		}
	}
	libusb_free_transfer(transfer);
}

static int busy_ack_retry_read(struct read_msg_data *udata)
{
	struct libusb_transfer *transfer;
	int r;

	transfer = alloc_send_cmdresponse_transfer(udata->dev, 0x09, NULL, 0,
		busy_ack_sent_cb, udata);
	if (!transfer)
		return -ENOMEM;

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(transfer->buffer);
		libusb_free_transfer(transfer);
	}
	return r;
}

/* Returns 0 if message was handled, 1 if it was a device-busy message, and
 * negative on error. */
static int __handle_incoming_msg(struct read_msg_data *udata,
	unsigned char *buf)
{
	uint16_t len = GUINT16_FROM_LE(((buf[5] & 0xf) << 8) | buf[6]);
	uint16_t computed_crc = udf_crc(buf + 4, len + 3);
	uint16_t msg_crc = GUINT16_FROM_LE((buf[len + 8] << 8) | buf[len + 7]);
	unsigned char *retdata = NULL;
	unsigned char code_a, code_b;

	if (computed_crc != msg_crc) {
		fp_err("CRC failed, got %04x expected %04x", msg_crc, computed_crc);
		return -1;
	}

	code_a = buf[4];
	code_b = buf[5] & 0xf0;
	len = GUINT16_FROM_LE(((buf[5] & 0xf) << 8) | buf[6]);
	fp_dbg("A=%02x B=%02x len=%d", code_a, code_b, len);

	if (code_a && !code_b) {
		/* device sends command to driver */
		fp_dbg("cmd %x from device to driver", code_a);

		if (code_a == 0x08) {
			int r;
			fp_dbg("device busy, send busy-ack");
			r = busy_ack_retry_read(udata);
			return (r < 0) ? r : 1;
		}

		if (len > 0) {
			retdata = g_malloc(len);
			memcpy(retdata, buf + 7, len);
		}
		udata->callback(udata->dev, READ_MSG_CMD, code_a, 0, retdata, len,
			udata->user_data);
		g_free(retdata);
	} else if (!code_a) {
		/* device sends response to a previously executed command */
		unsigned char *innerbuf = buf + 7;
		unsigned char _subcmd;
		uint16_t innerlen;

		if (len < 6) {
			fp_err("cmd response too short (%d)", len);
			return -1;
		}
		if (innerbuf[0] != 0x28) {
			fp_err("cmd response without 28 byte?");
			return -1;
		}

		/* not really sure what these 2 bytes are. on most people's hardware,
		 * these bytes are always 0. However, Alon Bar-Lev's hardware gives
		 * 0xfb 0xff during the READ28_OB initsm stage. so don't error out
		 * if they are different... */
		if (innerbuf[3] || innerbuf[4])
			fp_dbg("non-zero bytes in cmd response");

		innerlen = innerbuf[1] | (innerbuf[2] << 8);
		innerlen = GUINT16_FROM_LE(innerlen) - 3;
		_subcmd = innerbuf[5];
		fp_dbg("device responds to subcmd %x with %d bytes", _subcmd, innerlen);
		if (innerlen > 0) {
			retdata = g_malloc(innerlen);
			memcpy(retdata, innerbuf + 6, innerlen);
		}
		udata->callback(udata->dev, READ_MSG_RESPONSE, code_b, _subcmd,
			retdata, innerlen, udata->user_data);
		g_free(retdata);
	} else {
		fp_err("don't know how to handle this message");
		return -1;
	}
	return 0;
}

static void read_msg_extend_cb(struct libusb_transfer *transfer)
{
	struct read_msg_data *udata = transfer->user_data;
	unsigned char *buf = transfer->buffer - MSG_READ_BUF_SIZE;
	int handle_result = 0;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fp_err("extended msg read failed, code %d", transfer->status);
		goto err;
	}
	if (transfer->actual_length < transfer->length) {
		fp_err("extended msg short read (%d/%d)", transfer->actual_length,
			transfer->length);
		goto err;
	}

	handle_result = __handle_incoming_msg(udata, buf);
	if (handle_result < 0)
		goto err;
	goto out;

err:
	READ_MSG_DATA_CB_ERR(udata);
out:
	if (handle_result != 1)
		g_free(udata);
	g_free(buf);
	libusb_free_transfer(transfer);
}

static void read_msg_cb(struct libusb_transfer *transfer)
{
	struct read_msg_data *udata = transfer->user_data;
	unsigned char *data = transfer->buffer;
	uint16_t len;
	int handle_result = 0;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fp_err("async msg read failed, code %d", transfer->status);
		goto err;
	}
	if (transfer->actual_length < 9) {
		fp_err("async msg read too short (%d)", transfer->actual_length);
		goto err;
	}

	if (strncmp(data, "Ciao", 4) != 0) {
		fp_err("no Ciao for you!!");
		goto err;
	}

	len = GUINT16_FROM_LE(((data[5] & 0xf) << 8) | data[6]);
	if (transfer->actual_length != MSG_READ_BUF_SIZE
			&& (len + 9) > transfer->actual_length) {
		/* Check that the length claimed inside the message is in line with
		 * the amount of data that was transferred over USB. */
		fp_err("msg didn't include enough data, expected=%d recv=%d",
			len + 9, transfer->actual_length);
		goto err;
	}

	/* We use a 64 byte buffer for reading messages. However, sometimes
	 * messages are longer, in which case we have to do another USB bulk read
	 * to read the remainder. This is handled below. */
	if (len > MAX_DATA_IN_READ_BUF) {
		int needed = len - MAX_DATA_IN_READ_BUF;
		struct libusb_transfer *etransfer = fpi_usb_alloc();
		int r;

		fp_dbg("didn't fit in buffer, need to extend by %d bytes", needed);
		data = g_realloc((gpointer) data, MSG_READ_BUF_SIZE + needed);

		libusb_fill_bulk_transfer(etransfer, fpi_dev_get_usb_dev(udata->dev), EP_IN,
			data + MSG_READ_BUF_SIZE, needed, read_msg_extend_cb, udata,
			TIMEOUT);

		r = libusb_submit_transfer(etransfer);
		if (r < 0) {
			fp_err("extended read submission failed");
			/* FIXME memory leak here? */
			goto err;
		}
		libusb_free_transfer(transfer);
		return;
	}

	handle_result = __handle_incoming_msg(udata, data);
	if (handle_result < 0)
		goto err;
	goto out;

err:
	READ_MSG_DATA_CB_ERR(udata);
out:
	libusb_free_transfer(transfer);
	if (handle_result != 1)
		g_free(udata);
	g_free(data);
}

static int __read_msg_async(struct read_msg_data *udata)
{
	unsigned char *buf = g_malloc(MSG_READ_BUF_SIZE);
	struct libusb_transfer *transfer = fpi_usb_alloc();
	int r;

	libusb_fill_bulk_transfer(transfer, fpi_dev_get_usb_dev(udata->dev), EP_IN, buf,
		MSG_READ_BUF_SIZE, read_msg_cb, udata, TIMEOUT);
	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(buf);
		libusb_free_transfer(transfer);
	}

	return r;
}

static int read_msg_async(struct fp_dev *dev, read_msg_cb_fn callback,
	void *user_data)
{
	struct read_msg_data *udata = g_malloc(sizeof(*udata));
	int r;

	udata->dev = dev;
	udata->callback = callback;
	udata->user_data = user_data;
	r = __read_msg_async(udata);
	if (r)
		g_free(udata);
	return r;
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

/* device initialisation state machine */

enum initsm_states {
	WRITE_CTRL400 = 0,
	READ_MSG03,
	SEND_RESP03,
	READ_MSG05,
	SEND28_06,
	READ28_06,
	SEND28_07,
	READ28_07,
	SEND28_08,
	READ28_08,
	SEND28_0C,
	READ28_0C,
	SEND28_0B,
	READ28_0B,
	INITSM_NUM_STATES,
};

static void
initsm_read_msg_response_cb(fpi_ssm              *ssm,
			    struct fp_dev        *dev,
			    enum read_msg_status  status,
			    uint8_t               seq,
			    unsigned char         expect_subcmd,
			    unsigned char         subcmd)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	if (status != READ_MSG_RESPONSE) {
		fp_err("expected response, got %d seq=%x in state %d", status, seq,
			fpi_ssm_get_cur_state(ssm));
		fpi_ssm_mark_failed(ssm, -1);
	} else if (subcmd != expect_subcmd) {
		fp_warn("expected response to subcmd 0x%02x, got response to %02x in "
			"state %d", expect_subcmd, subcmd, fpi_ssm_get_cur_state(ssm));
		fpi_ssm_mark_failed(ssm, -1);
	} else if (seq != upekdev->seq) {
		fp_err("expected response to cmd seq=%02x, got response to %02x "
			"in state %d", upekdev->seq, seq, fpi_ssm_get_cur_state(ssm));
		fpi_ssm_mark_failed(ssm, -1);
	} else {
		fp_dbg("state %d completed", fpi_ssm_get_cur_state(ssm));
		fpi_ssm_next_state(ssm);
	}
}

static void read28_0b_cb(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	initsm_read_msg_response_cb((fpi_ssm *) user_data, dev, status, seq,
		0x0b, subcmd);
}

static void read28_0c_cb(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	initsm_read_msg_response_cb((fpi_ssm *) user_data, dev, status, seq,
		0x0c, subcmd);
}

static void read28_08_cb(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	initsm_read_msg_response_cb((fpi_ssm *) user_data, dev, status, seq,
		0x08, subcmd);
}

static void read28_07_cb(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	initsm_read_msg_response_cb((fpi_ssm *) user_data, dev, status, seq,
		0x07, subcmd);
}

static void read28_06_cb(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	initsm_read_msg_response_cb((fpi_ssm *) user_data, dev, status, seq,
		0x06, subcmd);
}

static void
initsm_read_msg_cmd_cb(fpi_ssm              *ssm,
		       struct fp_dev        *dev,
		       enum read_msg_status  status,
		       uint8_t               expect_seq,
		       uint8_t               seq)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	if (status == READ_MSG_ERROR) {
		fpi_ssm_mark_failed(ssm, -1);
		return;
	} else if (status != READ_MSG_CMD) {
		fp_err("expected command, got %d seq=%x in state %d", status, seq,
			fpi_ssm_get_cur_state(ssm));
		fpi_ssm_mark_failed(ssm, -1);
		return;
	}
	upekdev->seq = seq;
	if (seq != expect_seq) {
		fp_err("expected seq=%x, got %x in state %d", expect_seq, seq,
			fpi_ssm_get_cur_state(ssm));
		fpi_ssm_mark_failed(ssm, -1);
		return;
	}

	fpi_ssm_next_state(ssm);
}

static void read_msg05_cb(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	initsm_read_msg_cmd_cb((fpi_ssm *) user_data, dev, status, 5, seq);
}

static void read_msg03_cb(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	initsm_read_msg_cmd_cb((fpi_ssm *) user_data, dev, status, 3, seq);
}

static void ctrl400_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	/* FIXME check length? */
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED)
		fpi_ssm_next_state(ssm);
	else
		fpi_ssm_mark_failed(ssm, -1);
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void
initsm_read_msg_handler(fpi_ssm        *ssm,
			struct fp_dev  *dev,
			read_msg_cb_fn  callback)
{
	int r = read_msg_async(dev, callback, ssm);
	if (r < 0) {
		fp_err("async read msg failed in state %d", fpi_ssm_get_cur_state(ssm));
		fpi_ssm_mark_failed(ssm, r);
	}
}

static void initsm_send_msg_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED
			&& transfer->length == transfer->actual_length) {
		fp_dbg("state %d completed", fpi_ssm_get_cur_state(ssm));
		fpi_ssm_next_state(ssm);
	} else {
		fp_err("failed, state=%d rqlength=%d actual_length=%d", fpi_ssm_get_cur_state(ssm),
			transfer->length, transfer->actual_length);
		fpi_ssm_mark_failed(ssm, -1);
	}
	libusb_free_transfer(transfer);
}

static void
initsm_send_msg28_handler(fpi_ssm             *ssm,
			  struct fp_dev       *dev,
			  unsigned char        subcmd,
			  const unsigned char *data,
			  uint16_t             innerlen)
{
	struct libusb_transfer *transfer;
	int r;

	transfer = alloc_send_cmd28_transfer(dev, subcmd, data, innerlen,
		initsm_send_msg_cb, ssm);
	if (!transfer) {
		fpi_ssm_mark_failed(ssm, -ENOMEM);
		return;
	}

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		fp_err("urb submission failed error %d in state %d", r, fpi_ssm_get_cur_state(ssm));
		g_free(transfer->buffer);
		libusb_free_transfer(transfer);
		fpi_ssm_mark_failed(ssm, -EIO);
	}
}

static void initsm_run_state(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);
	struct libusb_transfer *transfer;
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case WRITE_CTRL400: ;
		unsigned char *data;

		transfer = fpi_usb_alloc();
		data = g_malloc(LIBUSB_CONTROL_SETUP_SIZE + 1);
		libusb_fill_control_setup(data,
			LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE, 0x0c, 0x100, 0x0400, 1);
		libusb_fill_control_transfer(transfer, fpi_dev_get_usb_dev(dev), data,
			ctrl400_cb, ssm, TIMEOUT);

		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(data);
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, r);
		}
		break;
	case READ_MSG03:
		initsm_read_msg_handler(ssm, dev, read_msg03_cb);
		break;
	case SEND_RESP03: ;
		transfer = alloc_send_cmdresponse_transfer(dev, ++upekdev->seq,
			init_resp03, sizeof(init_resp03), initsm_send_msg_cb, ssm);
		if (!transfer) {
			fpi_ssm_mark_failed(ssm, -ENOMEM);
			break;
		}

		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, r);
		}
		break;
	case READ_MSG05:
		initsm_read_msg_handler(ssm, dev, read_msg05_cb);
		break;
	case SEND28_06: ;
		unsigned char dummy28_06 = 0x04;
		upekdev->seq = 0xf0;
		initsm_send_msg28_handler(ssm, dev, 0x06, &dummy28_06, 1);
		break;
	case READ28_06:
		initsm_read_msg_handler(ssm, dev, read28_06_cb);
		break;
	case SEND28_07: ;
		unsigned char dummy28_07 = 0x04;
		initsm_send_msg28_handler(ssm, dev, 0x07, &dummy28_07, 1);
		break;
	case READ28_07:
		initsm_read_msg_handler(ssm, dev, read28_07_cb);
		break;
	case SEND28_08:
		initsm_send_msg28_handler(ssm, dev, 0x08, init28_08, sizeof(init28_08));
		break;
	case READ28_08:
		initsm_read_msg_handler(ssm, dev, read28_08_cb);
		break;
	case SEND28_0C:
		initsm_send_msg28_handler(ssm, dev, 0x0c, init28_0c, sizeof(init28_0c));
		break;
	case READ28_0C:
		initsm_read_msg_handler(ssm, dev, read28_0c_cb);
		break;
	case SEND28_0B:
		initsm_send_msg28_handler(ssm, dev, 0x0b, init28_0b, sizeof(init28_0b));
		break;
	case READ28_0B:
		initsm_read_msg_handler(ssm, dev, read28_0b_cb);
		break;
	}
}

static fpi_ssm *initsm_new(struct fp_dev *dev,
			   void          *user_data)
{
	return fpi_ssm_new(dev, initsm_run_state, INITSM_NUM_STATES, user_data);
}

enum deinitsm_states {
	SEND_RESP07 = 0,
	READ_MSG01,
	DEINITSM_NUM_STATES,
};

static void send_resp07_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
		fpi_ssm_mark_failed(ssm, -EIO);
	else if (transfer->length != transfer->actual_length)
		fpi_ssm_mark_failed(ssm, -EPROTO);
	else
		fpi_ssm_next_state(ssm);
	libusb_free_transfer(transfer);
}

static void read_msg01_cb(struct fp_dev *dev, enum read_msg_status status,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	fpi_ssm *ssm = user_data;
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	if (status == READ_MSG_ERROR) {
		fpi_ssm_mark_failed(ssm, -1);
		return;
	} else if (status != READ_MSG_CMD) {
		fp_err("expected command, got %d seq=%x", status, seq);
		fpi_ssm_mark_failed(ssm, -1);
		return;
	}
	upekdev->seq = seq;
	if (seq != 1) {
		fp_err("expected seq=1, got %x", seq);
		fpi_ssm_mark_failed(ssm, -1);
		return;
	}

	fpi_ssm_next_state(ssm);
}

static void deinitsm_state_handler(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case SEND_RESP07: ;
		struct libusb_transfer *transfer;
		unsigned char dummy = 0;

		transfer = alloc_send_cmdresponse_transfer(dev, 0x07, &dummy, 1,
			send_resp07_cb, ssm);
		if (!transfer) {
			fpi_ssm_mark_failed(ssm, -ENOMEM);
			break;
		}

		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, r);
		}
		break;
	case READ_MSG01: ;
		r = read_msg_async(dev, read_msg01_cb, ssm);
		if (r < 0)
			fpi_ssm_mark_failed(ssm, r);
		break;
	}
}

static fpi_ssm *deinitsm_new(struct fp_dev *dev)
{
	return fpi_ssm_new(dev, deinitsm_state_handler, DEINITSM_NUM_STATES, NULL);
}

static int dev_init(struct fp_dev *dev, unsigned long driver_data)
{
	struct upekts_dev *upekdev = NULL;
	int r;

	r = libusb_claim_interface(fpi_dev_get_usb_dev(dev), 0);
	if (r < 0) {
		fp_err("could not claim interface 0: %s", libusb_error_name(r));
		return r;
	}

	upekdev = g_malloc(sizeof(*upekdev));
	upekdev->seq = 0xf0; /* incremented to 0x00 before first cmd */
	fp_dev_set_instance_data(dev, upekdev);
	fpi_dev_set_nr_enroll_stages(dev, 3);

	fpi_drvcb_open_complete(dev, 0);
	return 0;
}

static void dev_exit(struct fp_dev *dev)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	libusb_release_interface(fpi_dev_get_usb_dev(dev), 0);
	g_free(upekdev);
	fpi_drvcb_close_complete(dev);
}

static const unsigned char enroll_init[] = {
	0x02, 0xc0, 0xd4, 0x01, 0x00, 0x04, 0x00, 0x08
};
static const unsigned char scan_comp[] = {
	0x12, 0xff, 0xff, 0xff, 0xff /* scan completion, prefixes print data */
};

/* used for enrollment and verification */
static const unsigned char poll_data[] = { 0x30, 0x01 };

enum enroll_start_sm_states {
	RUN_INITSM = 0,
	ENROLL_INIT,
	READ_ENROLL_MSG28,
	ENROLL_START_NUM_STATES,
};

/* Called when the device initialization state machine completes */
static void enroll_start_sm_cb_initsm(fpi_ssm *initsm, struct fp_dev *_dev, void *user_data)
{
	fpi_ssm *enroll_start_ssm = user_data;
	int error = fpi_ssm_get_error(initsm);

	fpi_ssm_free(initsm);
	if (error)
		fpi_ssm_mark_failed(enroll_start_ssm, error);
	else
		fpi_ssm_next_state(enroll_start_ssm);
}

/* called when enroll init URB has completed */
static void enroll_start_sm_cb_init(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
		fpi_ssm_mark_failed(ssm, -EIO);
	else if (transfer->length != transfer->actual_length)
		fpi_ssm_mark_failed(ssm, -EPROTO);
	else
		fpi_ssm_next_state(ssm);
	libusb_free_transfer(transfer);
}

static void enroll_start_sm_cb_msg28(struct fp_dev *dev,
	enum read_msg_status status, uint8_t seq, unsigned char subcmd,
	unsigned char *data, size_t data_len, void *user_data)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);
	fpi_ssm *ssm = user_data;

	if (status != READ_MSG_RESPONSE) {
		fp_err("expected response, got %d seq=%x", status, seq);
		fpi_ssm_mark_failed(ssm, -1);
	} else if (subcmd != 0) {
		fp_warn("expected response to subcmd 0, got response to %02x",
			subcmd);
		fpi_ssm_mark_failed(ssm, -1);
	} else if (seq != upekdev->seq) {
		fp_err("expected response to cmd seq=%02x, got response to %02x",
			upekdev->seq, seq);
		fpi_ssm_mark_failed(ssm, -1);
	} else {
		fpi_ssm_next_state(ssm);
	}
}

static void enroll_start_sm_run_state(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case RUN_INITSM: ;
		fpi_ssm *initsm = initsm_new(dev, ssm);
		fpi_ssm_start(initsm, enroll_start_sm_cb_initsm);
		break;
	case ENROLL_INIT: ;
		struct libusb_transfer *transfer;
		transfer = alloc_send_cmd28_transfer(dev, 0x02, enroll_init,
			sizeof(enroll_init), enroll_start_sm_cb_init, ssm);
		if (!transfer) {
			fpi_ssm_mark_failed(ssm, -ENOMEM);
			break;
		}

		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, r);
		}
		break;
	case READ_ENROLL_MSG28: ;
		/* FIXME: protocol misunderstanding here. device receives response
		 * to subcmd 0 after submitting subcmd 2? */
		/* actually this is probably a poll response? does the above cmd
		 * include a 30 01 poll somewhere? */
		r = read_msg_async(dev, enroll_start_sm_cb_msg28, ssm);
		if (r < 0)
			fpi_ssm_mark_failed(ssm, r);
		break;
	}
}

static void enroll_iterate(struct fp_dev *dev);

static void e_handle_resp00(struct fp_dev *dev, unsigned char *data,
	size_t data_len)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);
	unsigned char status;
	int result = 0;

	if (data_len != 14) {
		fp_err("received 3001 poll response of %lu bytes?", data_len);
		fpi_drvcb_enroll_stage_completed(dev, -EPROTO, NULL, NULL);
		return;
	}

	status = data[5];
	fp_dbg("poll result = %02x", status);

	switch (status) {
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x26:
	case 0x27:
	case 0x2e:
		/* if we previously completed a non-last enrollment stage, we'll
		 * get this code to indicate successful stage completion */
		if (upekdev->enroll_passed) {
			result = FP_ENROLL_PASS;
			upekdev->enroll_passed = FALSE;
		}
		/* otherwise it just means "no news" so we poll again */
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
		/* need to look at the next poll result to determine if enrollment is
		 * complete or not */
		upekdev->enroll_passed = 1;
		break;
	case 0x00: /* enrollment complete */
		/* we can now expect the enrollment data on the next poll, so we
		 * have nothing to do here */
		break;
	default:
		fp_err("unrecognised scan status code %02x", status);
		result = -EPROTO;
		break;
	}

	if (result) {
		fpi_drvcb_enroll_stage_completed(dev, result, NULL, NULL);
		if (result > 0)
			enroll_iterate(dev);
	} else {
		enroll_iterate(dev);
	}

	/* FIXME: need to extend protocol research to handle the case when
	 * enrolment fails, e.g. you scan a different finger on each stage */
	/* FIXME: should do proper tracking of when we expect cmd0 results and
	 * cmd2 results and enforce it */
}

static void e_handle_resp02(struct fp_dev *dev, unsigned char *data,
	size_t data_len)
{
	struct fp_print_data *fdata = NULL;
	struct fp_print_data_item *item = NULL;
	int result = -EPROTO;

	if (data_len < sizeof(scan_comp)) {
		fp_err("fingerprint data too short (%lu bytes)", data_len);
	} else if (memcmp(data, scan_comp, sizeof(scan_comp)) != 0) {
		fp_err("unrecognised data prefix %x %x %x %x %x",
			data[0], data[1], data[2], data[3], data[4]);
	} else {
		fdata = fpi_print_data_new(dev);
		item = fpi_print_data_item_new(data_len - sizeof(scan_comp));
		memcpy(item->data, data + sizeof(scan_comp),
			data_len - sizeof(scan_comp));
		fpi_print_data_add_item(fdata, item);

		result = FP_ENROLL_COMPLETE;
	}

	fpi_drvcb_enroll_stage_completed(dev, result, fdata, NULL);
}

static void enroll_iterate_msg_cb(struct fp_dev *dev,
	enum read_msg_status msgstat, uint8_t seq, unsigned char subcmd,
	unsigned char *data, size_t data_len, void *user_data)
{
	if (msgstat != READ_MSG_RESPONSE) {
		fp_err("expected response, got %d seq=%x", msgstat, seq);
		fpi_drvcb_enroll_stage_completed(dev, -EPROTO, NULL, NULL);
		return;
	}
	if (subcmd == 0) {
		e_handle_resp00(dev, data, data_len);
	} else if (subcmd == 2) {
		e_handle_resp02(dev, data, data_len);
	} else {
		fp_err("unexpected subcmd %d", subcmd);
		fpi_drvcb_enroll_stage_completed(dev, -EPROTO, NULL, NULL);
	}

}

static void enroll_iterate_cmd_cb(struct libusb_transfer *transfer)
{
	struct fp_dev *dev = transfer->user_data;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fpi_drvcb_enroll_stage_completed(dev, -EIO, NULL, NULL);
	} else if (transfer->length != transfer->actual_length) {
		fpi_drvcb_enroll_stage_completed(dev, -EPROTO, NULL, NULL);
	} else {
		int r = read_msg_async(dev, enroll_iterate_msg_cb, NULL);
		if (r < 0)
			fpi_drvcb_enroll_stage_completed(dev, r, NULL, NULL);
	}
	libusb_free_transfer(transfer);
}

static void enroll_iterate(struct fp_dev *dev)
{
	int r;
	struct libusb_transfer *transfer = alloc_send_cmd28_transfer(dev, 0x00,
		poll_data, sizeof(poll_data), enroll_iterate_cmd_cb, dev);

	if (!transfer) {
		fpi_drvcb_enroll_stage_completed(dev, -ENOMEM, NULL, NULL);
		return;
	}

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(transfer->buffer);
		libusb_free_transfer(transfer);
		fpi_drvcb_enroll_stage_completed(dev, -EIO, NULL, NULL);
	}
}

static void enroll_started(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	fpi_drvcb_enroll_started(dev, fpi_ssm_get_error(ssm));

	if (!fpi_ssm_get_error(ssm))
		enroll_iterate(dev);

	fpi_ssm_free(ssm);
}

static int enroll_start(struct fp_dev *dev)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	/* do_init state machine first */
	fpi_ssm *ssm = fpi_ssm_new(dev, enroll_start_sm_run_state,
		ENROLL_START_NUM_STATES, NULL);

	upekdev->enroll_passed = FALSE;
	fpi_ssm_start(ssm, enroll_started);
	return 0;
}

static void enroll_stop_deinit_cb(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	/* don't really care about errors */
	fpi_drvcb_enroll_stopped(dev);
	fpi_ssm_free(ssm);
}

static int enroll_stop(struct fp_dev *dev)
{
	fpi_ssm *ssm = deinitsm_new(dev);
	fpi_ssm_start(ssm, enroll_stop_deinit_cb);
	return 0;
}

static void verify_stop_deinit_cb(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	/* don't really care about errors */
	fpi_drvcb_verify_stopped(dev);
	fpi_ssm_free(ssm);
}

static void do_verify_stop(struct fp_dev *dev)
{
	fpi_ssm *ssm = deinitsm_new(dev);
	fpi_ssm_start(ssm, verify_stop_deinit_cb);
}

static const unsigned char verify_hdr[] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xc0, 0xd4, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
	0x00
};

enum {
	VERIFY_RUN_INITSM = 0,
	VERIFY_INIT,
	VERIFY_NUM_STATES,
};

/* Called when the device initialization state machine completes */
static void verify_start_sm_cb_initsm(fpi_ssm *initsm, struct fp_dev *_dev, void *user_data)
{
	fpi_ssm *verify_start_ssm = user_data;
	int err;

	err = fpi_ssm_get_error(initsm);
	if (err)
		fpi_ssm_mark_failed(verify_start_ssm, err);
	else
		fpi_ssm_next_state(verify_start_ssm);
	fpi_ssm_free(initsm);
}

static void verify_init_2803_cb(struct libusb_transfer *transfer)
{
	fpi_ssm *ssm = transfer->user_data;
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
		fpi_ssm_mark_failed(ssm, -EIO);
	else if (transfer->length != transfer->actual_length)
		fpi_ssm_mark_failed(ssm, -EPROTO);
	else
		fpi_ssm_next_state(ssm);
	libusb_free_transfer(transfer);
}

static void verify_start_sm_run_state(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case VERIFY_RUN_INITSM: ;
		fpi_ssm *initsm = initsm_new(dev, ssm);
		fpi_ssm_start(initsm, verify_start_sm_cb_initsm);
		break;
	case VERIFY_INIT: ;
		struct fp_print_data *print = fpi_dev_get_verify_data(dev);
		struct fp_print_data_item *item = fpi_print_data_get_item(print);
		size_t data_len = sizeof(verify_hdr) + item->length;
		unsigned char *data = g_malloc(data_len);
		struct libusb_transfer *transfer;

		memcpy(data, verify_hdr, sizeof(verify_hdr));
		memcpy(data + sizeof(verify_hdr), item->data, item->length);
		transfer = alloc_send_cmd28_transfer(dev, 0x03, data, data_len,
			verify_init_2803_cb, ssm);
		g_free(data);
		if (!transfer) {
			fpi_ssm_mark_failed(ssm, -ENOMEM);
			break;
		}

		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			fpi_ssm_mark_failed(ssm, -EIO);
		}
		break;
	}
}

static void verify_iterate(struct fp_dev *dev);

static void v_handle_resp00(struct fp_dev *dev, unsigned char *data,
	size_t data_len)
{
	unsigned char status;
	int r = 0;

	if (data_len != 14) {
		fp_err("received 3001 poll response of %lu bytes?", data_len);
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
		break;
	case 0x1c: /* FIXME what does this one mean? */
	case 0x0b: /* FIXME what does this one mean? */
	case 0x23: /* FIXME what does this one mean? */
		r = FP_VERIFY_RETRY;
		break;
	case 0x0f: /* scan taking too long, remove finger and try again */
		r = FP_VERIFY_RETRY_REMOVE_FINGER;
		break;
	case 0x1e: /* swipe too short */
		r = FP_VERIFY_RETRY_TOO_SHORT;
		break;
	case 0x24: /* finger not centered */
		r = FP_VERIFY_RETRY_CENTER_FINGER;
		break;
	default:
		fp_err("unrecognised verify status code %02x", status);
		r = -EPROTO;
	}

out:
	if (r)
		fpi_drvcb_report_verify_result(dev, r, NULL);
	if (r >= 0)
		verify_iterate(dev);
}

static void v_handle_resp03(struct fp_dev *dev, unsigned char *data,
	size_t data_len)
{
	int r;

	if (data_len < 2) {
		fp_err("verify result abnormally short!");
		r = -EPROTO;
	} else if (data[0] != 0x12) {
		fp_err("unexpected verify header byte %02x", data[0]);
		r = -EPROTO;
	} else if (data[1] == 0x00) {
		r = FP_VERIFY_NO_MATCH;
	} else if (data[1] == 0x01) {
		r = FP_VERIFY_MATCH;
	} else {
		fp_err("unrecognised verify result %02x", data[1]);
		r = -EPROTO;
	}
	fpi_drvcb_report_verify_result(dev, r, NULL);
}

static void verify_rd2800_cb(struct fp_dev *dev, enum read_msg_status msgstat,
	uint8_t seq, unsigned char subcmd, unsigned char *data, size_t data_len,
	void *user_data)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	if (msgstat != READ_MSG_RESPONSE) {
		fp_err("expected response, got %d seq=%x", msgstat, seq);
		fpi_drvcb_report_verify_result(dev, -EPROTO, NULL);
		return;
	} else if (seq != upekdev->seq) {
		fp_err("expected response to cmd seq=%02x, got response to %02x",
			upekdev->seq, seq);
		fpi_drvcb_report_verify_result(dev, -EPROTO, NULL);
		return;
	}

	if (subcmd == 0)
		v_handle_resp00(dev, data, data_len);
	else if (subcmd == 3)
		v_handle_resp03(dev, data, data_len);
	else
		fpi_drvcb_report_verify_result(dev, -EPROTO, NULL);
}

static void verify_wr2800_cb(struct libusb_transfer *transfer)
{
	struct fp_dev *dev = transfer->user_data;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fpi_drvcb_report_verify_result(dev, -EIO, NULL);
	} else if (transfer->length != transfer->actual_length) {
		fpi_drvcb_report_verify_result(dev, -EIO, NULL);
	} else {
		int r = read_msg_async(dev, verify_rd2800_cb, NULL);
		if (r < 0)
			fpi_drvcb_report_verify_result(dev, r, NULL);
	}
	libusb_free_transfer(transfer);
}

static void verify_iterate(struct fp_dev *dev)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	if (upekdev->stop_verify) {
		do_verify_stop(dev);
		return;
	}

	/* FIXME: this doesn't flow well, should the first cmd be moved from
	 * verify init to here? */
	if (upekdev->first_verify_iteration) {
		int r = read_msg_async(dev, verify_rd2800_cb, NULL);
		upekdev->first_verify_iteration = FALSE;
		if (r < 0)
			fpi_drvcb_report_verify_result(dev, r, NULL);
	} else {
		int r;
		struct libusb_transfer *transfer = alloc_send_cmd28_transfer(dev,
			0x00, poll_data, sizeof(poll_data), verify_wr2800_cb, dev);

		if (!transfer) {
			fpi_drvcb_report_verify_result(dev, -ENOMEM, NULL);
			return;
		}

		r = libusb_submit_transfer(transfer);
		if (r < 0) {
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			fpi_drvcb_report_verify_result(dev, -EIO, NULL);
		}
	}
}

static void verify_started(fpi_ssm *ssm, struct fp_dev *dev, void *user_data)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	fpi_drvcb_verify_started(dev, fpi_ssm_get_error(ssm));
	if (!fpi_ssm_get_error(ssm)) {
		upekdev->first_verify_iteration = TRUE;
		verify_iterate(dev);
	}

	fpi_ssm_free(ssm);
}

static int verify_start(struct fp_dev *dev)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);
	fpi_ssm *ssm = fpi_ssm_new(dev, verify_start_sm_run_state,
		VERIFY_NUM_STATES, NULL);
	upekdev->stop_verify = FALSE;
	fpi_ssm_start(ssm, verify_started);
	return 0;
}

static int verify_stop(struct fp_dev *dev, gboolean iterating)
{
	struct upekts_dev *upekdev = FP_INSTANCE_DATA(dev);

	if (!iterating)
		do_verify_stop(dev);
	else
		upekdev->stop_verify = TRUE;
	return 0;
}

static const struct usb_id id_table[] = {
	{ .vendor = 0x0483, .product = 0x2016 },
	{ 0, 0, 0, }, /* terminating entry */
};

struct fp_driver upekts_driver = {
	.id = UPEKTS_ID,
	.name = FP_COMPONENT,
	.full_name = "UPEK TouchStrip",
	.id_table = id_table,
	.scan_type = FP_SCAN_TYPE_SWIPE,
	.open = dev_init,
	.close = dev_exit,
	.enroll_start = enroll_start,
	.enroll_stop = enroll_stop,
	.verify_start = verify_start,
	.verify_stop = verify_stop,
};

