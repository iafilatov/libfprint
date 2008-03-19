/*
 * Digital Persona U.are.U 4000/4000B driver for libfprint
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

#define FP_COMPONENT "uru4000"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <openssl/aes.h>
#include <libusb.h>

#include <fp_internal.h>

#define EP_INTR			(1 | LIBUSB_ENDPOINT_IN)
#define EP_DATA			(2 | LIBUSB_ENDPOINT_IN)
#define USB_RQ			0x04
#define CTRL_IN			(LIBUSB_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRL_OUT		(LIBUSB_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)
#define CTRL_TIMEOUT	5000
#define BULK_TIMEOUT	5000
#define DATABLK_RQLEN	0x1b340
#define DATABLK_EXPECT	0x1b1c0
#define CAPTURE_HDRLEN	64
#define IRQ_LENGTH		64
#define CR_LENGTH		16

enum {
	IRQDATA_SCANPWR_ON = 0x56aa,
	IRQDATA_FINGER_ON = 0x0101,
	IRQDATA_FINGER_OFF = 0x0200,
	IRQDATA_DEATH = 0x0800,
};

enum {
	REG_HWSTAT = 0x07,
	REG_MODE = 0x4e,
	FIRMWARE_START = 0x100,
	REG_RESPONSE = 0x2000,
	REG_CHALLENGE = 0x2010,
};

enum {
	MODE_INIT = 0x00,
	MODE_AWAIT_FINGER_ON = 0x10,
	MODE_AWAIT_FINGER_OFF = 0x12,
	MODE_CAPTURE = 0x20,
	MODE_SHUT_UP = 0x30,
	MODE_READY = 0x80,
};

enum {
	MS_KBD,
	MS_INTELLIMOUSE,
	MS_STANDALONE,
	MS_STANDALONE_V2,
	DP_URU4000,
	DP_URU4000B,
};

static const struct uru4k_dev_profile {
	const char *name;
	uint16_t firmware_start;
	uint16_t fw_enc_offset;
	gboolean auth_cr;
} uru4k_dev_info[] = {
	[MS_KBD] = {
		.name = "Microsoft Keyboard with Fingerprint Reader",
		.fw_enc_offset = 0x411,
		.auth_cr = FALSE,
	},
	[MS_INTELLIMOUSE] = {
		.name = "Microsoft Wireless IntelliMouse with Fingerprint Reader",
		.fw_enc_offset = 0x411,
		.auth_cr = FALSE,
	},
	[MS_STANDALONE] = {
		.name = "Microsoft Fingerprint Reader",
		.fw_enc_offset = 0x411,
		.auth_cr = FALSE,
	},
	[MS_STANDALONE_V2] = {
		.name = "Microsoft Fingerprint Reader v2",
		.fw_enc_offset = 0x52e,
		.auth_cr = TRUE,	
	},
	[DP_URU4000] = {
		.name = "Digital Persona U.are.U 4000",
		.fw_enc_offset = 0x693,
		.auth_cr = FALSE,
	},
	[DP_URU4000B] = {
		.name = "Digital Persona U.are.U 4000B",
		.fw_enc_offset = 0x411,
		.auth_cr = FALSE,
	},
};

typedef void (*irq_cb_fn)(struct fp_img_dev *dev, int status, uint16_t type,
	void *user_data);
typedef void (*irqs_stopped_cb_fn)(struct fp_img_dev *dev);

struct uru4k_dev {
	const struct uru4k_dev_profile *profile;
	uint8_t interface;
	enum fp_imgdev_state activate_state;
	unsigned char last_hwstat_rd;

	struct libusb_transfer *irq_transfer;
	struct libusb_transfer *img_transfer;

	irq_cb_fn irq_cb;
	void *irq_cb_data;
	irqs_stopped_cb_fn irqs_stopped_cb;

	int rebootpwr_ctr;
	int powerup_ctr;
	unsigned char powerup_hwstat;

	int scanpwr_irq_timeouts;
	struct fpi_timeout *scanpwr_irq_timeout;

	AES_KEY aeskey;
};

/* For 2nd generation MS devices */
static const unsigned char crkey[] = {
	0x79, 0xac, 0x91, 0x79, 0x5c, 0xa1, 0x47, 0x8e,
	0x98, 0xe0, 0x0f, 0x3c, 0x59, 0x8f, 0x5f, 0x4b,
};

typedef void (*set_reg_cb_fn)(struct fp_img_dev *dev, int status,
	void *user_data);

struct set_reg_data {
	struct fp_img_dev *dev;
	set_reg_cb_fn callback;
	void *user_data;
};

static void set_reg_cb(struct libusb_transfer *transfer)
{
	struct set_reg_data *srdata = transfer->user_data;
	int r = 0;
	
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
		r = -EIO;
	else if (transfer->actual_length != 1)
		r = -EPROTO;

	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
	srdata->callback(srdata->dev, r, srdata->user_data);
	g_free(srdata);
}

static int set_reg(struct fp_img_dev *dev, uint16_t reg,
	unsigned char value, set_reg_cb_fn callback, void *user_data)
{
	struct set_reg_data *srdata;
	struct libusb_transfer *transfer = libusb_alloc_transfer();
	unsigned char *data;
	int r;

	if (!transfer)
		return -ENOMEM;

	srdata = g_malloc(sizeof(*srdata));
	srdata->dev = dev;
	srdata->callback = callback;
	srdata->user_data = user_data;

	data = g_malloc(LIBUSB_CONTROL_SETUP_SIZE + 1);
	data[LIBUSB_CONTROL_SETUP_SIZE] = value;
	libusb_fill_control_setup(data, CTRL_OUT, USB_RQ, reg, 0, 1);
	libusb_fill_control_transfer(transfer, dev->udev, data, set_reg_cb,
		srdata, CTRL_TIMEOUT);

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(srdata);
		g_free(data);
		libusb_free_transfer(transfer);
	}
	return r;
}

/*
 * HWSTAT
 *
 * This register has caused me a lot of headaches. It pretty much defines
 * code flow, and if you don't get it right, the pretty lights don't come on.
 * I think the situation is somewhat complicated by the fact that writing it
 * doesn't affect the read results in the way you'd expect -- but then again
 * it does have some obvious effects. Here's what we know
 *
 * BIT 7: LOW POWER MODE
 * When this bit is set, the device is partially turned off or something. Some
 * things, like firmware upload, need to be done in this state. But generally
 * we want to clear this bit during late initialization, which can sometimes
 * be tricky.
 *
 * BIT 2: SOMETHING WENT WRONG
 * Not sure about this, but see the init function, as when we detect it,
 * we reboot the device. Well, we mess with hwstat until this evil bit gets
 * cleared.
 *
 * BIT 1: IRQ PENDING
 * Just had a brainwave. This bit is set when the device is trying to deliver
 * and interrupt to the host. Maybe?
 */

typedef void (*challenge_response_cb)(struct fp_img_dev *dev, int result,
	void *user_data);

struct c_r_data {
	struct fp_img_dev *dev;
	challenge_response_cb callback;
	void *user_data;
};

static void response_cb(struct libusb_transfer *transfer)
{
	struct c_r_data *crdata = transfer->user_data;
	int r = 0;
	
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED)
		r = -EIO;
	else if (transfer->actual_length != CR_LENGTH)
		r = -EPROTO;

	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
	crdata->callback(crdata->dev, r, crdata->user_data);
}

static void challenge_cb(struct libusb_transfer *transfer)
{
	struct c_r_data *crdata = transfer->user_data;
	struct fp_img_dev *dev = crdata->dev;
	struct uru4k_dev *urudev = dev->priv;
	struct libusb_transfer *resp_transfer;
	unsigned char *respdata;
	int r;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		crdata->callback(crdata->dev, -EIO, crdata->user_data);
		goto out;
	} else if (transfer->actual_length != CR_LENGTH) {
		crdata->callback(crdata->dev, -EPROTO, crdata->user_data);
		goto out;
	}

	/* submit response */
	resp_transfer = libusb_alloc_transfer();
	if (!resp_transfer) {
		crdata->callback(crdata->dev, -ENOMEM, crdata->user_data);
		goto out;
	}

	respdata = g_malloc(LIBUSB_CONTROL_SETUP_SIZE + CR_LENGTH);
	libusb_fill_control_setup(respdata, CTRL_OUT, USB_RQ, REG_RESPONSE, 0,
		CR_LENGTH);
	libusb_fill_control_transfer(transfer, dev->udev, respdata, response_cb,
		crdata, CTRL_TIMEOUT);

	/* produce response from challenge */
	AES_encrypt(libusb_control_transfer_get_data(transfer),
		respdata + LIBUSB_CONTROL_SETUP_SIZE, &urudev->aeskey);

	r = libusb_submit_transfer(resp_transfer);
	if (r < 0) {
		g_free(respdata);
		libusb_free_transfer(resp_transfer);
		crdata->callback(crdata->dev, r, crdata->user_data);
	}

out:
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

/*
 * 2nd generation MS devices added an AES-based challenge/response
 * authentication scheme, where the device challenges the authenticity of the
 * driver.
 */
static int do_challenge_response(struct fp_img_dev *dev,
	challenge_response_cb callback, void *user_data)
{
	struct libusb_transfer *transfer = libusb_alloc_transfer();;
	struct c_r_data *crdata;
	unsigned char *data;
	int r;

	fp_dbg("");
	if (!transfer)
		return -ENOMEM;

	crdata = g_malloc(sizeof(*crdata));
	crdata->dev = dev;
	crdata->callback = callback;
	crdata->user_data = user_data;

	data = g_malloc(LIBUSB_CONTROL_SETUP_SIZE + CR_LENGTH);
	libusb_fill_control_setup(data, CTRL_IN, USB_RQ, REG_CHALLENGE, 0,
		CR_LENGTH);
	libusb_fill_control_transfer(transfer, dev->udev, data, challenge_cb,
		crdata, CTRL_TIMEOUT);

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(crdata);
		g_free(data);
		libusb_free_transfer(transfer);
	}
	return r;
}

/***** INTERRUPT HANDLING *****/

#define IRQ_HANDLER_IS_RUNNING(urudev) ((urudev)->irq_transfer)

static int start_irq_handler(struct fp_img_dev *dev);

static void irq_handler(struct libusb_transfer *transfer)
{
	struct fp_img_dev *dev = transfer->user_data;
	struct uru4k_dev *urudev = dev->priv;
	unsigned char *data = transfer->buffer;
	uint16_t type;
	int r = 0;

	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		fp_dbg("cancelled");
		if (urudev->irqs_stopped_cb)
			urudev->irqs_stopped_cb(dev);
		urudev->irqs_stopped_cb = NULL;
		goto out;
	} else if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		r = -EIO;
		goto err;
	} else if (transfer->actual_length != transfer->length) {
		fp_err("short interrupt read? %d", transfer->actual_length);
		r = -EPROTO;
		goto err;
	}

	type = GUINT16_FROM_BE(*((uint16_t *) data));
	fp_dbg("recv irq type %04x", type);
	g_free(data);
	libusb_free_transfer(transfer);

	/* The 0800 interrupt seems to indicate imminent failure (0 bytes transfer)
	 * of the next scan. It still appears on occasion. */
	if (type == IRQDATA_DEATH)
		fp_warn("oh no! got the interrupt OF DEATH! expect things to go bad");

	if (urudev->irq_cb)
		urudev->irq_cb(dev, 0, type, urudev->irq_cb_data);
	else
		fp_dbg("ignoring interrupt");

	r = start_irq_handler(dev);
	if (r == 0)
		return;

	transfer = NULL;
	data = NULL;
err:
	if (urudev->irq_cb)
		urudev->irq_cb(dev, r, 0, urudev->irq_cb_data);
out:
	g_free(data);
	libusb_free_transfer(transfer);
	urudev->irq_transfer = NULL;
}

static int start_irq_handler(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;
	struct libusb_transfer *transfer = libusb_alloc_transfer();
	unsigned char *data;
	int r;

	if (!transfer)
		return -ENOMEM;
	
	data = g_malloc(IRQ_LENGTH);
	libusb_fill_bulk_transfer(transfer, dev->udev, EP_INTR, data, IRQ_LENGTH,
		irq_handler, dev, 0);

	urudev->irq_transfer = transfer;
	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(data);
		libusb_free_transfer(transfer);
		urudev->irq_transfer = NULL;
	}
	return r;
}

static void stop_irq_handler(struct fp_img_dev *dev, irqs_stopped_cb_fn cb)
{
	struct uru4k_dev *urudev = dev->priv;
	struct libusb_transfer *transfer = urudev->irq_transfer;
	if (transfer) {
		libusb_cancel_transfer(transfer);
		urudev->irqs_stopped_cb = cb;
	}
}

/***** IMAGING LOOP *****/

static int start_imaging_loop(struct fp_img_dev *dev);

static void image_cb(struct libusb_transfer *transfer)
{
	struct fp_img_dev *dev = transfer->user_data;
	struct uru4k_dev *urudev = dev->priv;
	int hdr_skip = CAPTURE_HDRLEN;
	int image_size = DATABLK_EXPECT - CAPTURE_HDRLEN;
	struct fp_img *img;
	int r = 0;

	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		fp_dbg("cancelled");
		urudev->img_transfer = NULL;
		g_free(transfer->buffer);
		libusb_free_transfer(transfer);
		return;
	} else if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		r = -EIO;
		goto out;
	}

	if (transfer->actual_length == image_size) {
		/* no header! this is rather odd, but it happens sometimes with my MS
		 * keyboard */
		fp_dbg("got image with no header!");
		hdr_skip = 0;
	} else if (transfer->actual_length != DATABLK_EXPECT) {
		fp_err("unexpected image capture size (%d)", transfer->actual_length);
		r = -EPROTO;
		goto out;
	}

	img = fpi_img_new(image_size);
	memcpy(img->data, transfer->buffer + hdr_skip, image_size);
	img->flags = FP_IMG_V_FLIPPED | FP_IMG_H_FLIPPED | FP_IMG_COLORS_INVERTED;
	fpi_imgdev_image_captured(dev, img);

out:
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
	if (r == 0)
		r = start_imaging_loop(dev);

	if (r)
		fpi_imgdev_session_error(dev, r);
}

static int start_imaging_loop(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;
	struct libusb_transfer *transfer = libusb_alloc_transfer();
	unsigned char *data;
	int r;

	if (!transfer)
		return -ENOMEM;
	
	data = g_malloc(DATABLK_RQLEN);
	libusb_fill_bulk_transfer(transfer, dev->udev, EP_DATA, data,
		DATABLK_RQLEN, image_cb, dev, 0);

	urudev->img_transfer = transfer;
	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(data);
		libusb_free_transfer(transfer);
	}

	return r;
}

static void stop_imaging_loop(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;
	struct libusb_transfer *transfer = urudev->img_transfer;
	if (transfer)
		libusb_cancel_transfer(transfer);
	/* FIXME: should probably wait for cancellation to complete */
}

/***** STATE CHANGING *****/

static void finger_presence_irq_cb(struct fp_img_dev *dev, int status,
	uint16_t type, void *user_data)
{
	if (status)
		fpi_imgdev_session_error(dev, status);
	else if (type == IRQDATA_FINGER_ON)
		fpi_imgdev_report_finger_status(dev, TRUE);
	else if (type == IRQDATA_FINGER_OFF)
		fpi_imgdev_report_finger_status(dev, FALSE);
	else
		fp_warn("ignoring unexpected interrupt %04x", type);
}

static void change_state_set_reg_cb(struct fp_img_dev *dev, int status,
	void *user_data)
{
	if (status)
		fpi_imgdev_session_error(dev, status);
}

static int dev_change_state(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	struct uru4k_dev *urudev = dev->priv;

	stop_imaging_loop(dev);

	switch (state) {
	case IMGDEV_STATE_AWAIT_FINGER_ON:
		if (!IRQ_HANDLER_IS_RUNNING(urudev))
			return -EIO;
		urudev->irq_cb = finger_presence_irq_cb;
		return set_reg(dev, REG_MODE, MODE_AWAIT_FINGER_ON,
			change_state_set_reg_cb, NULL);

	case IMGDEV_STATE_CAPTURE:
		urudev->irq_cb = NULL;
		start_imaging_loop(dev);
		return set_reg(dev, REG_MODE, MODE_CAPTURE, change_state_set_reg_cb,
			NULL);

	case IMGDEV_STATE_AWAIT_FINGER_OFF:
		if (!IRQ_HANDLER_IS_RUNNING(urudev))
			return -EIO;
		urudev->irq_cb = finger_presence_irq_cb;
		return set_reg(dev, REG_MODE, MODE_AWAIT_FINGER_OFF,
			change_state_set_reg_cb, NULL);

	default:
		fp_err("unrecognised state %d", state);
		return -EINVAL;
	}
}

/***** GENERIC STATE MACHINE HELPER FUNCTIONS *****/

static void sm_set_reg_cb(struct fp_img_dev *dev, int result, void *user_data)
{
	struct fpi_ssm *ssm = user_data;

	if (result)
		fpi_ssm_mark_aborted(ssm, result);
	else
		fpi_ssm_next_state(ssm);
}

static void sm_set_reg(struct fpi_ssm *ssm, uint16_t reg,
	unsigned char value)
{
	struct fp_img_dev *dev = ssm->priv;
	int r = set_reg(dev, reg, value, sm_set_reg_cb, ssm);
	if (r < 0)
		fpi_ssm_mark_aborted(ssm, r);
}

static void sm_set_mode(struct fpi_ssm *ssm, unsigned char mode)
{
	fp_dbg("mode %02x", mode);
	sm_set_reg(ssm, REG_MODE, mode);
}

static void sm_set_hwstat(struct fpi_ssm *ssm, unsigned char value)
{
	fp_dbg("set %02x", value);
	sm_set_reg(ssm, REG_HWSTAT, value);
}

static void sm_get_hwstat_cb(struct libusb_transfer *transfer)
{
	struct fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fpi_ssm_mark_aborted(ssm, -EIO);
	} else if (transfer->actual_length != 1) {
		fpi_ssm_mark_aborted(ssm, -EPROTO);
	} else {
		urudev->last_hwstat_rd = libusb_control_transfer_get_data(transfer)[0];
		fp_dbg("value %02x", urudev->last_hwstat_rd);
		fpi_ssm_next_state(ssm);
	}
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void sm_get_hwstat(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct libusb_transfer *transfer = libusb_alloc_transfer();
	unsigned char *data;
	int r;

	if (!transfer) {
		fpi_ssm_mark_aborted(ssm, -ENOMEM);
		return;
	}

	data = g_malloc(LIBUSB_CONTROL_SETUP_SIZE + 1);

	/* The windows driver uses a request of 0x0c here. We use 0x04 to be
	 * consistent with every other command we know about. */
	/* FIXME is the above comment still true? */
	libusb_fill_control_setup(data, CTRL_IN, USB_RQ, REG_HWSTAT, 0, 1);
	libusb_fill_control_transfer(transfer, dev->udev, data, sm_get_hwstat_cb,
		ssm, CTRL_TIMEOUT);

	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(data);
		libusb_free_transfer(transfer);
		fpi_ssm_mark_aborted(ssm, -EIO);
	}
}

static void sm_fix_fw_read_cb(struct libusb_transfer *transfer)
{
	struct fpi_ssm *ssm = transfer->user_data;
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;
	unsigned char new;
	unsigned char fwenc;
	uint16_t enc_addr = FIRMWARE_START + urudev->profile->fw_enc_offset;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fpi_ssm_mark_aborted(ssm, -EIO);
		goto out;
	} else if (transfer->actual_length != 1) {
		fpi_ssm_mark_aborted(ssm, -EPROTO);
		goto out;
	}

	fwenc = libusb_control_transfer_get_data(transfer)[0];
	fp_dbg("firmware encryption byte at %x reads %02x", enc_addr, fwenc);
	if (fwenc != 0x07 && fwenc != 0x17)
		fp_dbg("strange encryption byte value, please report this");

	new = fwenc & 0xef;
	if (new == fwenc) {
		fpi_ssm_next_state(ssm);
	} else {
		fp_dbg("fixed encryption byte to %02x", new);
		sm_set_reg(ssm, enc_addr, new);
	}

out:
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
}

static void sm_fix_firmware(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;
	uint16_t enc_addr = FIRMWARE_START + urudev->profile->fw_enc_offset;
	struct libusb_transfer *transfer = libusb_alloc_transfer();
	unsigned char *data;
	int r;

	if (!transfer) {
		fpi_ssm_mark_aborted(ssm, -ENOMEM);
		return;
	}

	data = g_malloc(LIBUSB_CONTROL_SETUP_SIZE + 1);
	libusb_fill_control_setup(data, 0xc0, 0x0c, enc_addr, 0, 1);
	libusb_fill_control_transfer(transfer, dev->udev, data, sm_fix_fw_read_cb,
		ssm, CTRL_TIMEOUT);
	
	r = libusb_submit_transfer(transfer);
	if (r < 0) {
		g_free(data);
		libusb_free_transfer(transfer);
		fpi_ssm_mark_aborted(ssm, r);
	}
}

/***** INITIALIZATION *****/

/* After closing an app and setting hwstat to 0x80, my ms keyboard gets in a
 * confused state and returns hwstat 0x85. On next app run, we don't get the
 * 56aa interrupt. This is the best way I've found to fix it: mess around
 * with hwstat until it starts returning more recognisable values. This
 * doesn't happen on my other devices: uru4000, uru4000b, ms fp rdr v2 
 *
 * The windows driver copes with this OK, but then again it uploads firmware
 * right after reading the 0x85 hwstat, allowing some time to pass before it
 * attempts to tweak hwstat again...
 *
 * This is implemented with a reboot power state machine. the ssm runs during
 * initialization if bits 2 and 7 are set in hwstat. it masks off the 4 high
 * hwstat bits then checks that bit 1 is set. if not, it pauses before reading
 * hwstat again. machine completes when reading hwstat shows bit 1 is set,
 * and fails after 100 tries. */

enum rebootpwr_states {
	REBOOTPWR_SET_HWSTAT = 0,
	REBOOTPWR_GET_HWSTAT,
	REBOOTPWR_CHECK_HWSTAT,
	REBOOTPWR_PAUSE,
	REBOOTPWR_NUM_STATES,
};

static void rebootpwr_pause_cb(void *data)
{
	struct fpi_ssm *ssm = data;
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;

	if (!--urudev->rebootpwr_ctr) {
		fp_err("could not reboot device power");
		fpi_ssm_mark_aborted(ssm, -EIO);
	} else {
		fpi_ssm_jump_to_state(ssm, REBOOTPWR_GET_HWSTAT);
	}
}

static void rebootpwr_run_state(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;

	switch (ssm->cur_state) {
	case REBOOTPWR_SET_HWSTAT:
		urudev->rebootpwr_ctr = 100;
		sm_set_hwstat(ssm, urudev->last_hwstat_rd & 0xf);
		break;
	case REBOOTPWR_GET_HWSTAT:
		sm_get_hwstat(ssm);
		break;
	case REBOOTPWR_CHECK_HWSTAT:
		if (urudev->last_hwstat_rd & 0x1)
			fpi_ssm_mark_completed(ssm);
		else
			fpi_ssm_next_state(ssm);
		break;
	case REBOOTPWR_PAUSE:
		if (fpi_timeout_add(10, rebootpwr_pause_cb, ssm) == NULL)
			fpi_ssm_mark_aborted(ssm, -ETIME);
		break;
	}
}

/* After messing with the device firmware in it's low-power state, we have to
 * power it back up and wait for interrupt notification. It's not quite as easy
 * as that: the combination of both modifying firmware *and* doing C-R auth on
 * my ms fp v2 device causes us not to get to get the 56aa interrupt and
 * for the hwstat write not to take effect. We have to loop a few times,
 * authenticating each time, until the device wakes up.
 *
 * This is implemented as the powerup state machine below. Pseudo-code:

	status = get_hwstat();
	for (i = 0; i < 100; i++) {
		set_hwstat(status & 0xf);
		if ((get_hwstat() & 0x80) == 0)
			break;

		usleep(10000);
		if (need_auth_cr)
			auth_cr();
	}

	if (tmp & 0x80)
		error("could not power up device");

 */

enum powerup_states {
	POWERUP_INIT = 0,
	POWERUP_SET_HWSTAT,
	POWERUP_GET_HWSTAT,
	POWERUP_CHECK_HWSTAT,
	POWERUP_PAUSE,
	POWERUP_CHALLENGE_RESPONSE,
	POWERUP_NUM_STATES,
};

static void powerup_pause_cb(void *data)
{
	struct fpi_ssm *ssm = data;
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;

	if (!--urudev->powerup_ctr) {
		fp_err("could not power device up");
		fpi_ssm_mark_aborted(ssm, -EIO);
	} else if (!urudev->profile->auth_cr) {
		fpi_ssm_jump_to_state(ssm, POWERUP_SET_HWSTAT);
	} else {
		fpi_ssm_next_state(ssm);
	}
}

static void powerup_challenge_response_cb(struct fp_img_dev *dev, int result,
	void *data)
{
	struct fpi_ssm *ssm = data;
	if (result)
		fpi_ssm_mark_aborted(ssm, result);
	else
		fpi_ssm_jump_to_state(ssm, POWERUP_SET_HWSTAT);
}

static void powerup_run_state(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;
	int r;

	switch (ssm->cur_state) {
	case POWERUP_INIT:
		urudev->powerup_ctr = 100;
		urudev->powerup_hwstat = urudev->last_hwstat_rd & 0xf;
		fpi_ssm_next_state(ssm);
		break;
	case POWERUP_SET_HWSTAT:
		sm_set_hwstat(ssm, urudev->powerup_hwstat);
		break;
	case POWERUP_GET_HWSTAT:
		sm_get_hwstat(ssm);
		break;
	case POWERUP_CHECK_HWSTAT:
		if ((urudev->last_hwstat_rd & 0x80) == 0)
			fpi_ssm_mark_completed(ssm);
		else
			fpi_ssm_next_state(ssm);
		break;
	case POWERUP_PAUSE:
		if (fpi_timeout_add(10, powerup_pause_cb, ssm) == NULL)
			fpi_ssm_mark_aborted(ssm, -ETIME);
		break;
	case POWERUP_CHALLENGE_RESPONSE:
		r = do_challenge_response(dev, powerup_challenge_response_cb, ssm);
		if (r < 0)
			fpi_ssm_mark_aborted(ssm, r);
		break;
	}
}

/*
 * This is the main initialization state machine. As pseudo-code:

	status = get_hwstat();

	// correct device power state
	if ((status & 0x84) == 0x84)
		run_reboot_sm();

	// power device down
	if ((status & 0x80) == 0)
		set_hwstat(status | 0x80);

	// disable encryption
	fwenc = read_firmware_encryption_byte();
	new = fwenc & 0xef;
	if (new != fwenc)
		write_firmware_encryption_byte(new);

	// power device up
	run_powerup_sm();
	await_irq(IRQDATA_SCANPWR_ON);
 */

enum init_states {
	INIT_GET_HWSTAT = 0,
	INIT_CHECK_HWSTAT_REBOOT,
	INIT_REBOOT_POWER,
	INIT_CHECK_HWSTAT_POWERDOWN,
	INIT_FIX_FIRMWARE,
	INIT_POWERUP,
	INIT_AWAIT_SCAN_POWER,
	INIT_DONE,
	INIT_NUM_STATES,
};

static void init_scanpwr_irq_cb(struct fp_img_dev *dev, int status,
	uint16_t type, void *user_data)
{
	struct fpi_ssm *ssm = user_data;

	if (status)
		fpi_ssm_mark_aborted(ssm, status);
	else if (type != IRQDATA_SCANPWR_ON)
		fp_dbg("ignoring interrupt");
	else if (ssm->cur_state != INIT_AWAIT_SCAN_POWER)
		fp_err("ignoring scanpwr interrupt due to being in wrong state %d",
			ssm->cur_state);
	else
		fpi_ssm_next_state(ssm);
}

static void init_scanpwr_timeout(void *user_data)
{
	struct fpi_ssm *ssm = user_data;
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;

	fp_warn("powerup timed out");
	urudev->irq_cb = NULL;
	urudev->scanpwr_irq_timeout = NULL;

	if (++urudev->scanpwr_irq_timeouts >= 3) {
		fp_err("powerup timed out 3 times, giving up");
		fpi_ssm_mark_aborted(ssm, -ETIMEDOUT);
	} else {
		fpi_ssm_jump_to_state(ssm, INIT_GET_HWSTAT);
	}
}

static void init_run_state(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;

	switch (ssm->cur_state) {
	case INIT_GET_HWSTAT:
		sm_get_hwstat(ssm);
		break;
	case INIT_CHECK_HWSTAT_REBOOT:
		if ((urudev->last_hwstat_rd & 0x84) == 0x84)
			fpi_ssm_next_state(ssm);
		else
			fpi_ssm_jump_to_state(ssm, INIT_CHECK_HWSTAT_POWERDOWN);
		break;
	case INIT_REBOOT_POWER: ;
		struct fpi_ssm *rebootsm = fpi_ssm_new(dev->dev, rebootpwr_run_state,
			REBOOTPWR_NUM_STATES);
		rebootsm->priv = dev;
		fpi_ssm_start_subsm(ssm, rebootsm);
		break;
	case INIT_CHECK_HWSTAT_POWERDOWN:
		if ((urudev->last_hwstat_rd & 0x80) == 0)
			sm_set_hwstat(ssm, urudev->last_hwstat_rd | 0x80);
		else
			fpi_ssm_next_state(ssm);
		break;
	case INIT_FIX_FIRMWARE:
		sm_fix_firmware(ssm);
		break;
	case INIT_POWERUP: ;
		struct fpi_ssm *powerupsm = fpi_ssm_new(dev->dev, powerup_run_state,
			POWERUP_NUM_STATES);
		powerupsm->priv = dev;
		fpi_ssm_start_subsm(ssm, powerupsm);
		break;
	case INIT_AWAIT_SCAN_POWER:
		if (!IRQ_HANDLER_IS_RUNNING(urudev)) {
			fpi_ssm_mark_aborted(ssm, -EIO);
			break;
		}

		/* sometimes the 56aa interrupt that we are waiting for never arrives,
		 * so we include this timeout loop to retry the whole process 3 times
		 * if we don't get an irq any time soon. */
		urudev->scanpwr_irq_timeout = fpi_timeout_add(300,
			init_scanpwr_timeout, ssm);
		if (!urudev->scanpwr_irq_timeout) {
			fpi_ssm_mark_aborted(ssm, -ETIME);
			break;
		}

		urudev->irq_cb_data = ssm;
		urudev->irq_cb = init_scanpwr_irq_cb;
		break;
	case INIT_DONE:
		fpi_timeout_cancel(urudev->scanpwr_irq_timeout);
		urudev->scanpwr_irq_timeout = NULL;
		urudev->irq_cb_data = NULL;
		urudev->irq_cb = NULL;
		fpi_ssm_mark_completed(ssm);
		break;
	}
}

static void activate_initsm_complete(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	struct uru4k_dev *urudev = dev->priv;
	int r = ssm->error;
	fpi_ssm_free(ssm);

	if (r) {
		fpi_imgdev_activate_complete(dev, r);
		return;
	}

	r = dev_change_state(dev, urudev->activate_state);
	fpi_imgdev_activate_complete(dev, r);
}

/* FIXME: having state parameter here is kinda useless, will we ever
 * see a scenario where the parameter is useful so early on in the activation
 * process? asynchronity means that it'll only be used in a later function
 * call. */
static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	struct uru4k_dev *urudev = dev->priv;
	struct fpi_ssm *ssm;
	int r;

	r = start_irq_handler(dev);
	if (r < 0)
		return r;

	urudev->scanpwr_irq_timeouts = 0;
	urudev->activate_state = state;
	ssm = fpi_ssm_new(dev->dev, init_run_state, INIT_NUM_STATES);
	ssm->priv = dev;
	fpi_ssm_start(ssm, activate_initsm_complete);
	return 0;
}

/***** DEINITIALIZATION *****/

enum deinit_states {
	DEINIT_SET_MODE_INIT = 0,
	DEINIT_POWERDOWN,
	DEINIT_NUM_STATES,
};

static void deinit_run_state(struct fpi_ssm *ssm)
{
	switch (ssm->cur_state) {
	case DEINIT_SET_MODE_INIT:
		sm_set_mode(ssm, MODE_INIT);
		break;
	case DEINIT_POWERDOWN:
		sm_set_hwstat(ssm, 0x80);
		break;
	}
}

static void deactivate_irqs_stopped(struct fp_img_dev *dev)
{
	fpi_imgdev_deactivate_complete(dev);
}

static void deactivate_deinitsm_complete(struct fpi_ssm *ssm)
{
	struct fp_img_dev *dev = ssm->priv;
	fpi_ssm_free(ssm);
	stop_irq_handler(dev, deactivate_irqs_stopped);
}

static void dev_deactivate(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;
	struct fpi_ssm *ssm = fpi_ssm_new(dev->dev, deinit_run_state,
		DEINIT_NUM_STATES);

	stop_imaging_loop(dev);
	urudev->irq_cb = NULL;
	urudev->irq_cb_data = NULL;
	ssm->priv = dev;
	fpi_ssm_start(ssm, deactivate_deinitsm_complete);
}

/***** LIBRARY STUFF *****/

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	struct libusb_config_descriptor *config;
	struct libusb_interface *iface = NULL;
	struct libusb_interface_descriptor *iface_desc;
	struct libusb_endpoint_descriptor *ep;
	struct uru4k_dev *urudev;
	int i;
	int r;

	/* Find fingerprint interface */
	config = libusb_get_config_descriptor(libusb_get_device(dev->udev));
	for (i = 0; i < config->bNumInterfaces; i++) {
		struct libusb_interface *cur_iface = &config->interface[i];

		if (cur_iface->num_altsetting < 1)
			continue;

		iface_desc = &cur_iface->altsetting[0];
		if (iface_desc->bInterfaceClass == 255
				&& iface_desc->bInterfaceSubClass == 255 
				&& iface_desc->bInterfaceProtocol == 255) {
			iface = cur_iface;
			break;
		}
	}

	if (iface == NULL) {
		fp_err("could not find interface");
		return -ENODEV;
	}

	/* Find/check endpoints */

	if (iface_desc->bNumEndpoints != 2) {
		fp_err("found %d endpoints!?", iface_desc->bNumEndpoints);
		return -ENODEV;
	}

	ep = &iface_desc->endpoint[0];
	if (ep->bEndpointAddress != EP_INTR
			|| (ep->bmAttributes & LIBUSB_ENDPOINT_TYPE_MASK) !=
				LIBUSB_ENDPOINT_TYPE_INTERRUPT) {
		fp_err("unrecognised interrupt endpoint");
		return -ENODEV;
	}

	ep = &iface_desc->endpoint[1];
	if (ep->bEndpointAddress != EP_DATA
			|| (ep->bmAttributes & LIBUSB_ENDPOINT_TYPE_MASK) !=
				LIBUSB_ENDPOINT_TYPE_BULK) {
		fp_err("unrecognised bulk endpoint");
		return -ENODEV;
	}

	/* Device looks like a supported reader */

	r = libusb_claim_interface(dev->udev, iface_desc->bInterfaceNumber);
	if (r < 0) {
		fp_err("interface claim failed");
		return r;
	}

	urudev = g_malloc0(sizeof(*urudev));
	urudev->profile = &uru4k_dev_info[driver_data];
	urudev->interface = iface_desc->bInterfaceNumber;
	AES_set_encrypt_key(crkey, 128, &urudev->aeskey);
	dev->priv = urudev;
	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

static void dev_deinit(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;
	libusb_release_interface(dev->udev, urudev->interface);
	g_free(urudev);
	fpi_imgdev_close_complete(dev);
}

static const struct usb_id id_table[] = {
	/* ms kbd with fp rdr */
	{ .vendor = 0x045e, .product = 0x00bb, .driver_data = MS_KBD },

	/* ms intellimouse with fp rdr */
	{ .vendor = 0x045e, .product = 0x00bc, .driver_data = MS_INTELLIMOUSE },

	/* ms fp rdr (standalone) */
	{ .vendor = 0x045e, .product = 0x00bd, .driver_data = MS_STANDALONE },

	/* ms fp rdr (standalone) v2 */
	{ .vendor = 0x045e, .product = 0x00ca, .driver_data = MS_STANDALONE_V2 },

	/* dp uru4000 (standalone) */
	{ .vendor = 0x05ba, .product = 0x0007, .driver_data = DP_URU4000 },

	/* dp uru4000b (standalone) */
	{ .vendor = 0x05ba, .product = 0x000a, .driver_data = DP_URU4000B },

	/* terminating entry */
	{ 0, 0, 0, },
};

struct fp_img_driver uru4000_driver = {
	.driver = {
		.id = 2,
		.name = FP_COMPONENT,
		.full_name = "Digital Persona U.are.U 4000/4000B",
		.id_table = id_table,
	},
	.flags = FP_IMGDRV_SUPPORTS_UNCONDITIONAL_CAPTURE,
	.img_height = 289,
	.img_width = 384,

	.open = dev_init,
	.close = dev_deinit,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
	.change_state = dev_change_state,
};

