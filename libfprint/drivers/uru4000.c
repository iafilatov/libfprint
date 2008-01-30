/*
 * Digital Persona U.are.U 4000/4000B driver for libfprint
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
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
#define DATABLK1_RQLEN	0x10000
#define DATABLK2_RQLEN	0xb340
#define DATABLK2_EXPECT	0xb1c0
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

struct uru4k_dev {
	const struct uru4k_dev_profile *profile;
	uint8_t interface;
	AES_KEY aeskey;
};

/* For 2nd generation MS devices */
static const unsigned char crkey[] = {
	0x79, 0xac, 0x91, 0x79, 0x5c, 0xa1, 0x47, 0x8e,
	0x98, 0xe0, 0x0f, 0x3c, 0x59, 0x8f, 0x5f, 0x4b,
};

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

static int get_hwstat(struct fp_img_dev *dev, unsigned char *data)
{
	int r;

	/* The windows driver uses a request of 0x0c here. We use 0x04 to be
	 * consistent with every other command we know about. */
	struct libusb_control_transfer msg = {
		.requesttype = CTRL_IN,
		.request = USB_RQ,
		.value = REG_HWSTAT,
		.index = 0,
		.length = 1,
		.data = data,
	};

	r = libusb_control_transfer(dev->udev, &msg, CTRL_TIMEOUT);
	if (r < 0) {
		fp_err("error %d", r);
		return r;
	} else if (r < 1) {
		fp_err("read too short (%d)", r);
		return -EIO;
	}

	fp_dbg("val=%02x", *data);
	return 0;
}

static int set_hwstat(struct fp_img_dev *dev, unsigned char data)
{
	int r;
	struct libusb_control_transfer msg = {
		.requesttype = CTRL_OUT,
		.request = USB_RQ,
		.value = REG_HWSTAT,
		.index = 0,
		.length = 1,
		.data = &data,
	};

	fp_dbg("val=%02x", data);
	r = libusb_control_transfer(dev->udev, &msg, CTRL_TIMEOUT);
	if (r < 0) {
		fp_err("error %d", r);
		return r;
	} else if (r < 1) {
		fp_err("read too short (%d)", r);
		return -EIO;
	}

	return 0;
}

static int set_mode(struct fp_img_dev *dev, unsigned char mode)
{
	int r;
	struct libusb_control_transfer msg = {
		.requesttype = CTRL_OUT,
		.request = USB_RQ,
		.value = REG_MODE,
		.index = 0,
		.length = 1,
		.data = &mode,
	};

	fp_dbg("%02x", mode);
	r = libusb_control_transfer(dev->udev, &msg, CTRL_TIMEOUT);
	if (r < 0) {
		fp_err("error %d", r);
		return r;
	} else if (r < 1) {
		fp_err("write too short (%d)", r);
		return -EIO;
	}

	return 0;
}

static int read_challenge(struct fp_img_dev *dev, unsigned char *data)
{
	int r;
	struct libusb_control_transfer msg = {
		.requesttype = CTRL_IN,
		.request = USB_RQ,
		.value = REG_CHALLENGE,
		.index = 0,
		.length = CR_LENGTH,
		.data = data,
	};

	r = libusb_control_transfer(dev->udev, &msg, CTRL_TIMEOUT);
	if (r < 0) {
		fp_err("error %d", r);
		return r;
	} else if (r < CR_LENGTH) {
		fp_err("read too short (%d)", r);
		return -EIO;
	}

	return 0;
}

static int write_response(struct fp_img_dev *dev, unsigned char *data)
{
	int r;
	struct libusb_control_transfer msg = {
		.requesttype = CTRL_OUT,
		.request = USB_RQ,
		.value = REG_RESPONSE,
		.index = 0,
		.length = CR_LENGTH,
		.data = data,
	};

	r = libusb_control_transfer(dev->udev, &msg, CTRL_TIMEOUT);
	if (r < 0) {
		fp_err("error %d", r);
		return r;
	} else if (r < 1) {
		fp_err("write too short (%d)", r);
		return -EIO;
	}

	return 0;
}

/*
 * 2nd generation MS devices added an AES-based challenge/response
 * authentication scheme, where the device challenges the authenticity of the
 * driver.
 */
static int auth_cr(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;
	unsigned char challenge[CR_LENGTH];
	unsigned char response[CR_LENGTH];
	int r;

	fp_dbg("");

	r = read_challenge(dev, challenge);
	if (r < 0) {
		fp_err("error %d reading challenge", r);
		return r;
	}

	AES_encrypt(challenge, response, &urudev->aeskey);

	r = write_response(dev, response);
	if (r < 0)
		fp_err("error %d writing response", r);

	return r;
}

static int get_irq(struct fp_img_dev *dev, unsigned char *buf, int timeout)
{
	uint16_t type;
	int r;
	int infinite_timeout = 0;
	int transferred;
	struct libusb_bulk_transfer msg = {
		.endpoint = EP_INTR,
		.data = buf,
		.length = IRQ_LENGTH,
	};

	if (timeout == 0) {
		infinite_timeout = 1;
		timeout = 1000;
	}

	/* Darwin and Linux behave inconsistently with regard to infinite timeouts.
	 * Linux accepts a timeout value of 0 as infinite timeout, whereas darwin
	 * returns -ETIMEDOUT immediately when a 0 timeout is used. We use a
	 * looping hack until libusb is fixed.
	 * See http://thread.gmane.org/gmane.comp.lib.libusb.devel.general/1315 */

retry:
	r = libusb_interrupt_transfer(dev->udev, &msg, &transferred, timeout);
	if (r == -ETIMEDOUT && infinite_timeout)
		goto retry;

	if (r < 0) {
		if (r != -ETIMEDOUT)
			fp_err("interrupt read failed, error %d", r);
		return r;
	} else if (transferred < IRQ_LENGTH) {
		fp_err("received %d byte IRQ!?", r);
		return -EIO;
	}

	type = GUINT16_FROM_BE(*((uint16_t *) buf));
	fp_dbg("irq type %04x", type);

	/* The 0800 interrupt seems to indicate imminent failure (0 bytes transfer)
	 * of the next scan. I think I've stopped it from coming up, not sure
	 * though! */
	if (type == IRQDATA_DEATH)
		fp_warn("oh no! got the interrupt OF DEATH! expect things to go bad");

	return 0;
}

enum get_irq_status {
	GET_IRQ_SUCCESS = 0,
	GET_IRQ_OVERFLOW = 1,
};

static int get_irq_with_type(struct fp_img_dev *dev,
	uint16_t irqtype, int timeout)
{
	uint16_t hdr;
	int discarded = 0;
	unsigned char irqbuf[IRQ_LENGTH];

	fp_dbg("type=%04x", irqtype);

	do {
		int r = get_irq(dev, irqbuf, timeout);
		if (r < 0)
			return r;

		hdr = GUINT16_FROM_BE(*((uint16_t *) irqbuf));
		if (hdr == irqtype)
			break;
		discarded++;
	} while (discarded < 3);

	if (discarded > 0)
		fp_dbg("discarded %d interrupts", discarded);

	if (hdr == irqtype) {
		return GET_IRQ_SUCCESS;
	} else {
		/* I've seen several cases where we're waiting for the 56aa powerup
		 * interrupt, but instead we just get three 0200 interrupts and then
		 * nothing. My theory is that the device can only queue 3 interrupts,
		 * or something. So, if we discard 3, ask the caller to retry whatever
		 * it was doing. */
		fp_dbg("possible IRQ overflow detected!");
		return GET_IRQ_OVERFLOW;
	}
}

static int await_finger_on(struct fp_img_dev *dev)
{
	int r;

retry:
	r = set_mode(dev, MODE_AWAIT_FINGER_ON);
	if (r < 0)
		return r;

	r = get_irq_with_type(dev, IRQDATA_FINGER_ON, 0);
	if (r == GET_IRQ_OVERFLOW)
		goto retry;
	else
		return r;
}

static int await_finger_off(struct fp_img_dev *dev)
{
	int r;

retry:
	r = set_mode(dev, MODE_AWAIT_FINGER_OFF);
	if (r < 0)
		return r;
	
	r = get_irq_with_type(dev, IRQDATA_FINGER_OFF, 0);
	if (r == GET_IRQ_OVERFLOW)
		goto retry;
	else
		return r;
}

static int capture(struct fp_img_dev *dev, gboolean unconditional,
	struct fp_img **ret)
{
	int r;
	struct fp_img *img;
	size_t image_size = DATABLK1_RQLEN + DATABLK2_EXPECT - CAPTURE_HDRLEN;
	int hdr_skip = CAPTURE_HDRLEN;
	int transferred;
	struct libusb_bulk_transfer msg1 = {
		.endpoint = EP_DATA,
		.length = DATABLK1_RQLEN,
	};
	struct libusb_bulk_transfer msg2 = {
		.endpoint = EP_DATA,
		.length = DATABLK2_RQLEN,
	};


	r = set_mode(dev, MODE_CAPTURE);
	if (r < 0)
		return r;

	/* The image is split up into 2 blocks over 2 USB transactions, which are
	 * joined contiguously. The image is prepended by a 64 byte header which
	 * we completely ignore.
	 *
	 * We mimic the windows driver behaviour by requesting 0xb340 bytes in the
	 * 2nd request, but we only expect 0xb1c0 in response. However, our buffers
	 * must be set up on the offchance that we receive as much data as we
	 * asked for. */

	img = fpi_img_new(DATABLK1_RQLEN + DATABLK2_RQLEN);
	msg1.data = img->data;
	msg2.data = img->data + DATABLK1_RQLEN;

	r = libusb_bulk_transfer(dev->udev, &msg1, &transferred, BULK_TIMEOUT);
	if (r < 0) {
		fp_err("part 1 capture failed, error %d", r);
		goto err;
	} else if (transferred < DATABLK1_RQLEN) {
		fp_err("part 1 capture too short (%d)", r);
		r = -EIO;
		goto err;
	}

	r = libusb_bulk_transfer(dev->udev, &msg2, &transferred, BULK_TIMEOUT);
	if (r < 0) {
		fp_err("part 2 capture failed, error %d", r);
		goto err;
	} else if (transferred != DATABLK2_EXPECT) {
		if (r == DATABLK2_EXPECT - CAPTURE_HDRLEN) {
			/* this is rather odd, but it happens sometimes with my MS
			 * keyboard */
			fp_dbg("got image with no header!");
			hdr_skip = 0;
		} else {
			fp_err("unexpected part 2 capture size (%d)", r);
			r = -EIO;
			goto err;
		}
	}

	/* remove header and shrink allocation */
	g_memmove(img->data, img->data + hdr_skip, image_size);
	img = fpi_img_resize(img, image_size);
	img->flags = FP_IMG_V_FLIPPED | FP_IMG_H_FLIPPED | FP_IMG_COLORS_INVERTED;

	*ret = img;
	return 0;
err:
	fp_img_free(img);
	return r;
}

static int fix_firmware(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;
	uint32_t enc_addr = FIRMWARE_START + urudev->profile->fw_enc_offset;
	unsigned char val, new;
	int r;
	struct libusb_control_transfer msg = {
		.requesttype = 0xc0,
		.request = 0x0c,
		.value = enc_addr,
		.index = 0,
		.data = &val,
		.length = 1,
	};

	r = libusb_control_transfer(dev->udev, &msg, CTRL_TIMEOUT);
	if (r < 0)
		return r;
	
	fp_dbg("encryption byte at %x reads %02x", enc_addr, val);
	if (val != 0x07 && val != 0x17)
		fp_dbg("strange encryption byte value, please report this");

	new = val & 0xef;
	//new = 0x17;
	if (new == val)
		return 0;

	msg.requesttype = 0x40;
	msg.request = 0x04;
	msg.data = &new;

	r = libusb_control_transfer(dev->udev, &msg, CTRL_TIMEOUT);
	if (r < 0)
		return r;

	fp_dbg("fixed encryption byte to %02x", new);
	return 1;
}

static int do_init(struct fp_img_dev *dev)
{
	unsigned char status;
	unsigned char tmp;
	struct uru4k_dev *urudev = dev->priv;
	gboolean need_auth_cr = urudev->profile->auth_cr;
	int timeouts = 0;
	int i;
	int r;

retry:
	r = get_hwstat(dev, &status);
	if (r < 0)
		return r;

	/* After closing an app and setting hwstat to 0x80, my ms keyboard
	 * gets in a confused state and returns hwstat 0x85. On next app run,
	 * we don't get the 56aa interrupt. This is the best way I've found to
	 * fix it: mess around with hwstat until it starts returning more
	 * recognisable values. This doesn't happen on my other devices:
	 * uru4000, uru4000b, ms fp rdr v2 
	 * The windows driver copes with this OK, but then again it uploads
	 * firmware right after reading the 0x85 hwstat, allowing some time
	 * to pass before it attempts to tweak hwstat again... */
	if ((status & 0x84) == 0x84) {
		fp_dbg("rebooting device power");
		r = set_hwstat(dev, status & 0xf);
		if (r < 0)
			return r;

		for (i = 0; i < 100; i++) {
			r = get_hwstat(dev, &status);
			if (r < 0)
				return r;
			if (status & 0x1)
				break;
			usleep(10000);
		}
		if ((status & 0x1) == 0) {
			fp_err("could not reboot device power");
			return -EIO;
		}
	}
	
	if ((status & 0x80) == 0) {
		status |= 0x80;
		r = set_hwstat(dev, status);
		if (r < 0)
			return r;
	}


	r = fix_firmware(dev);
	if (r < 0)
		return r;

	/* Power up device and wait for interrupt notification */
	/* The combination of both modifying firmware *and* doing C-R auth on
	 * my ms fp v2 device causes us not to get to get the 56aa interrupt and
	 * for the hwstat write not to take effect. We loop a few times,
	 * authenticating each time, until the device wakes up. */
	for (i = 0; i < 100; i++) { /* max 1 sec */
		r = set_hwstat(dev, status & 0xf);
		if (r < 0)
			return r;

		r = get_hwstat(dev, &tmp);
		if (r < 0)
			return r;

		if ((tmp & 0x80) == 0)
			break;

		usleep(10000);

		if (need_auth_cr) {
			r = auth_cr(dev);
			if (r < 0)
				return r;
		}
	}

	if (tmp & 0x80) {
		fp_err("could not power up device");
		return -EIO;
	}

	r = get_irq_with_type(dev, IRQDATA_SCANPWR_ON, 300);
	if (r == GET_IRQ_OVERFLOW) {
		goto retry;
	} else if (r == -ETIMEDOUT) {
		timeouts++;
		if (timeouts <= 3) {
			fp_dbg("scan power up timeout, retrying...");
			goto retry;
		} else {
			fp_err("could not power up scanner after 3 attempts");
		}
	}
	return r;
}

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
	config = libusb_dev_get_config(libusb_devh_get_dev(dev->udev));
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

	r = do_init(dev);
	if (r < 0)
		goto err;

	return 0;
err:
	libusb_release_interface(dev->udev, iface_desc->bInterfaceNumber);
	g_free(urudev);
	return r;
}

static void dev_exit(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;

	set_mode(dev, MODE_INIT);
	set_hwstat(dev, 0x80);
	libusb_release_interface(dev->udev, urudev->interface);
	g_free(urudev);
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

	.init = dev_init,
	.exit = dev_exit,
	.await_finger_on = await_finger_on,
	.await_finger_off = await_finger_off,
	.capture = capture,
};

