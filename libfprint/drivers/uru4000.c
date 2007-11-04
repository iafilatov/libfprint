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

#include <usb.h>

#include <fp_internal.h>

#define EP_INTR			(1 | USB_ENDPOINT_IN)
#define EP_DATA			(2 | USB_ENDPOINT_IN)
#define USB_RQ			0x04
#define CTRL_IN			(USB_TYPE_VENDOR | USB_ENDPOINT_IN)
#define CTRL_OUT		(USB_TYPE_VENDOR | USB_ENDPOINT_OUT)
#define CTRL_TIMEOUT	5000
#define BULK_TIMEOUT	5000
#define DATABLK1_RQLEN	0x10000
#define DATABLK2_RQLEN	0xb340
#define DATABLK2_EXPECT	0xb1c0
#define CAPTURE_HDRLEN	64
#define IRQ_LENGTH		64

enum {
	IRQDATA_SCANPWR_ON = 0x56aa,
	IRQDATA_FINGER_ON = 0x0101,
	IRQDATA_FINGER_OFF = 0x0200,
	IRQDATA_DEATH = 0x0800,
};

enum {
	REG_HWSTAT = 0x07,
	REG_MODE = 0x4e,
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
} uru4k_dev_info[] = {
	[MS_KBD] = {
		.name = "Microsoft Keyboard with Fingerprint Reader",
		.firmware_start = 0x100,
		.fw_enc_offset = 0x42b,
	},
	[MS_INTELLIMOUSE] = {
		.name = "Microsoft Wireless IntelliMouse with Fingerprint Reader",
		.firmware_start = 0x100,
		.fw_enc_offset = 0x42b,
	},
	[MS_STANDALONE] = {
		.name = "Microsoft Fingerprint Reader",
		.firmware_start = 0x100,
		.fw_enc_offset = 0x42b,
	},
	[DP_URU4000B] = {
		.name = "Digital Persona U.are.U 4000B",
		.firmware_start = 0x100,
		.fw_enc_offset = 0x42b,
	},
};

struct uru4k_dev {
	uint8_t interface;
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
	r = usb_control_msg(dev->udev, CTRL_IN, USB_RQ, REG_HWSTAT, 0,
		data, 1, CTRL_TIMEOUT);
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
	fp_dbg("val=%02x", data);

	r = usb_control_msg(dev->udev, CTRL_OUT, USB_RQ, REG_HWSTAT, 0,
		&data, 1, CTRL_TIMEOUT);
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

	fp_dbg("%02x", mode);
	r = usb_control_msg(dev->udev, CTRL_OUT, USB_RQ, REG_MODE, 0, &mode, 1,
		CTRL_TIMEOUT);
	if (r < 0) {
		fp_err("error %d", r);
		return r;
	} else if (r < 1) {
		fp_err("write too short (%d)", r);
		return -EIO;
	}

	return 0;
}

static int get_irq(struct fp_img_dev *dev, unsigned char *buf, int timeout)
{
	uint16_t type;
	int r;
	int infinite_timeout = 0;

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
	r = usb_interrupt_read(dev->udev, EP_INTR, buf, IRQ_LENGTH, timeout);
	if (r == -ETIMEDOUT && infinite_timeout)
		goto retry;

	if (r < 0) {
		if (r != -ETIMEDOUT)
			fp_err("interrupt read failed, error %d", r);
		return r;
	} else if (r < IRQ_LENGTH) {
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

	r = usb_bulk_read(dev->udev, EP_DATA, img->data, DATABLK1_RQLEN,
		BULK_TIMEOUT);
	if (r < 0) {
		fp_err("part 1 capture failed, error %d", r);
		goto err;
	} else if (r < DATABLK1_RQLEN) {
		fp_err("part 1 capture too short (%d)", r);
		r = -EIO;
		goto err;
	}

	r = usb_bulk_read(dev->udev, EP_DATA, img->data + DATABLK1_RQLEN,
		DATABLK2_RQLEN, BULK_TIMEOUT);
	if (r < 0) {
		fp_err("part 2 capture failed, error %d", r);
		goto err;
	} else if (r != DATABLK2_EXPECT) {
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
	g_free(img);
	return r;
}

static int do_init(struct fp_img_dev *dev)
{
	unsigned char status;
	unsigned char tmp;
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

	/* FIXME fix firmware (disable encryption) */

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

		/* FIXME do C-R auth for v2 devices */
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
	struct usb_config_descriptor *config;
	struct usb_interface *iface = NULL;
	struct usb_interface_descriptor *iface_desc;
	struct usb_endpoint_descriptor *ep;
	struct uru4k_dev *urudev;
	int i;
	int r;

	/* Find fingerprint interface */
	config = usb_device(dev->udev)->config;
	for (i = 0; i < config->bNumInterfaces; i++) {
		struct usb_interface *cur_iface = &config->interface[i];

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
			|| (ep->bmAttributes & USB_ENDPOINT_TYPE_MASK) !=
				USB_ENDPOINT_TYPE_INTERRUPT) {
		fp_err("unrecognised interrupt endpoint");
		return -ENODEV;
	}

	ep = &iface_desc->endpoint[1];
	if (ep->bEndpointAddress != EP_DATA
			|| (ep->bmAttributes & USB_ENDPOINT_TYPE_MASK) !=
				USB_ENDPOINT_TYPE_BULK) {
		fp_err("unrecognised bulk endpoint");
		return -ENODEV;
	}

	/* Device looks like a supported reader */

	r = usb_claim_interface(dev->udev, iface_desc->bInterfaceNumber);
	if (r < 0) {
		fp_err("interface claim failed");
		return r;
	}

	urudev = g_malloc0(sizeof(*urudev));
	urudev->interface = iface_desc->bInterfaceNumber;
	dev->priv = urudev;

	r = do_init(dev);
	if (r < 0)
		goto err;

	return 0;
err:
	usb_release_interface(dev->udev, iface_desc->bInterfaceNumber);
	g_free(urudev);
	return r;
}

static void dev_exit(struct fp_img_dev *dev)
{
	struct uru4k_dev *urudev = dev->priv;

	set_mode(dev, MODE_INIT);
	set_hwstat(dev, 0x80);
	usb_release_interface(dev->udev, urudev->interface);
	g_free(urudev);
}

static const struct usb_id id_table[] = {
	/* ms kbd with fp rdr */
	{ .vendor = 0x045e, .product = 0x00bb, .driver_data = MS_KBD },

	/* ms intellimouse with fp rdr */
	{ .vendor = 0x045e, .product = 0x00bc, .driver_data = MS_INTELLIMOUSE },

	/* ms fp rdr (standalone) */
	{ .vendor = 0x045e, .product = 0x00bd, .driver_data = MS_STANDALONE },

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

