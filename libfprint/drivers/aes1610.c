/*
 * AuthenTec AES1610 driver for libfprint
 * Copyright (C) 2007 Anthony Bretaudeau <wxcover@users.sourceforge.net>
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2007 Cyrille Bagard
 * Copyright (C) 2007 Vasily Khoruzhick
 *
 * Based on code from http://home.gna.org/aes2501, relicensed with permission
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

#define FP_COMPONENT "aes1610"

#include <errno.h>
#include <string.h>

#include <usb.h>

#include <aeslib.h>
#include <fp_internal.h>

/* FIXME these need checking */
#define EP_IN			(1 | USB_ENDPOINT_IN)
#define EP_OUT			(2 | USB_ENDPOINT_OUT)

#define BULK_TIMEOUT 4000

#define FIRST_AES1610_REG	0x1B
#define LAST_AES1610_REG	0xFF

/*
 * The AES1610 is an imaging device using a swipe-type sensor. It samples
 * the finger at preprogrammed intervals, sending a 128x8 frame to the
 * computer.
 * Unless the user is scanning their finger unreasonably fast, the frames
 * *will* overlap. The implementation below detects this overlap and produces
 * a contiguous image as the end result.
 * The fact that the user determines the length of the swipe (and hence the
 * number of useful frames) and also the fact that overlap varies means that
 * images returned from this driver vary in height.
 */

#define FRAME_WIDTH		128
#define FRAME_HEIGHT	8
#define FRAME_SIZE		(FRAME_WIDTH * FRAME_HEIGHT)
/* maximum number of frames to read during a scan */
/* FIXME reduce substantially */
#define MAX_FRAMES		350

static int read_data(struct fp_img_dev *dev, unsigned char *data, size_t len)
{
	int r;
	fp_dbg("len=%zd", len);

	r = usb_bulk_read(dev->udev, EP_IN, data, len, BULK_TIMEOUT);
	if (r < 0) {
		fp_err("bulk read error %d", r);
		return r;
	} else if (r < (int) len) {
		fp_err("unexpected short read %d/%zd", r, len);
		return -EIO;
	}
	return 0;
}

static const struct aes_regwrite init[] = {
	{ 0x82, 0x00 }
};

static const struct aes_regwrite stop_reader[] = {
	{ 0xFF, 0x00 }
};

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	int r;

	r = usb_claim_interface(dev->udev, 0);
	if (r < 0) {
		fp_err("could not claim interface 0");
		return r;
	}

	/* FIXME check endpoints */

	return aes_write_regv(dev, init, G_N_ELEMENTS(init));
}

static int do_exit(struct fp_img_dev *dev)
{
	return aes_write_regv(dev, stop_reader, G_N_ELEMENTS(stop_reader));
}

static void dev_exit(struct fp_img_dev *dev)
{
	do_exit(dev);
	usb_release_interface(dev->udev, 0);
}

static const struct aes_regwrite finger_det_reqs[] = {
	{ 0x80, 0x01 },
	{ 0x80, 0x12 }, 
	{ 0x85, 0x00 }, 
	{ 0x8A, 0x00 },
	{ 0x8B, 0x0E },
	{ 0x8C, 0x90 }, 
	{ 0x8D, 0x83 }, 
	{ 0x8E, 0x07 }, 
	{ 0x8F, 0x07 }, 
	{ 0x96, 0x00 },
	{ 0x97, 0x48 }, 
	{ 0xA1, 0x00 }, 
	{ 0xA2, 0x50 }, 
	{ 0xA6, 0xE4 }, 
	{ 0xAD, 0x08 },
	{ 0xAE, 0x5B }, 
	{ 0xAF, 0x54 }, 
	{ 0xB1, 0x28 }, 
	{ 0xB5, 0xAB }, 
	{ 0xB6, 0x0E },
	{ 0x1B, 0x2D }, 
	{ 0x81, 0x04 }
};

static const struct aes_regwrite finger_det_none[] = {
	{ 0x80, 0x01 },
	{ 0x82, 0x00 }, 
	{ 0x86, 0x00 }, 
	{ 0xB1, 0x28 }, 
	{ 0x1D, 0x00 }
};

static int detect_finger(struct fp_img_dev *dev)
{
	unsigned char buffer[19];
	int r;
	int i;
	int sum = 0;

	r = aes_write_regv(dev, finger_det_reqs, G_N_ELEMENTS(finger_det_reqs));
	if (r < 0)
		return r;

	r = read_data(dev, buffer, 19);
	if (r < 0)
		return r;

	for (i = 3; i < 17; i++)
		sum += (buffer[i] & 0xf) + (buffer[i] >> 4);
		
	/* We need to answer something if no finger has been detected */
	if (sum <= 20) {
		r = aes_write_regv(dev, finger_det_none, G_N_ELEMENTS(finger_det_none));
		if (r < 0)
			return r;
	}
	
	return sum > 20;
}

static int await_finger_on(struct fp_img_dev *dev)
{
	int r;
	do {
		r = detect_finger(dev);
	} while (r == 0);
	return (r < 0) ? r : 0;
}

/* find overlapping parts of frames */
static unsigned int find_overlap(unsigned char *first_frame,
	unsigned char *second_frame, unsigned int *min_error)
{
	unsigned int dy;
	unsigned int not_overlapped_height = 0;
	*min_error = 255 * FRAME_SIZE;
	for (dy = 0; dy < FRAME_HEIGHT; dy++) {
		/* Calculating difference (error) between parts of frames */
		unsigned int i;
		unsigned int error = 0;
		for (i = 0; i < FRAME_WIDTH * (FRAME_HEIGHT - dy); i++) {
			/* Using ? operator to avoid abs function */
			error += first_frame[i] > second_frame[i] ? 
					(first_frame[i] - second_frame[i]) :
					(second_frame[i] - first_frame[i]); 
		}
		
		/* Normalize error */
		error *= 15;
		error /= i;
		if (error < *min_error) {
			*min_error = error;
			not_overlapped_height = dy;
		}
		first_frame += FRAME_WIDTH;
	}
	
	return not_overlapped_height; 
}

/* assemble a series of frames into a single image */
static unsigned int assemble(unsigned char *input, unsigned char *output,
	int num_strips, gboolean reverse, unsigned int *errors_sum)
{
	uint8_t *assembled = output;
	int frame;
	uint32_t image_height = FRAME_HEIGHT;
	unsigned int min_error;
	*errors_sum = 0;

	if (num_strips < 1)
		return 0;
	
	/* Rotating given data by 90 degrees 
	 * Taken from document describing aes1610 image format
	 * TODO: move reversing detection here */
	
	if (reverse)
		output += (num_strips - 1) * FRAME_SIZE;
	for (frame = 0; frame < num_strips; frame++) {
		aes_assemble_image(input, FRAME_WIDTH, FRAME_HEIGHT, output);
		input += FRAME_WIDTH * (FRAME_HEIGHT / 2);

		if (reverse)
			output -= FRAME_SIZE;
		else
			output += FRAME_SIZE;
	}

	/* Detecting where frames overlaped */
	output = assembled;
	for (frame = 1; frame < num_strips; frame++) {
		int not_overlapped;

		output += FRAME_SIZE;
		not_overlapped = find_overlap(assembled, output, &min_error);
		*errors_sum += min_error;
		image_height += not_overlapped;
		assembled += FRAME_WIDTH * not_overlapped;
		memcpy(assembled, output, FRAME_SIZE); 
	}
	return image_height;
}

static const struct aes_regwrite capture_reqs[] = {
	{ 0x80, 0x01 },
	{ 0x80, 0x12 },
	{ 0x84, 0x01 },
	{ 0x85, 0x00 },
	{ 0x89, 0x64 },
	{ 0x8A, 0x00 },
	{ 0x8B, 0x0E },
	{ 0x8C, 0x90 },
	{ 0xBE, 0x23 },
	{ 0x29, 0x06 },
	{ 0x2A, 0x35 },
	{ 0x96, 0x00 },
	{ 0x98, 0x03 },
	{ 0x99, 0x00 },
	{ 0x9C, 0xA5 },
	{ 0x9D, 0x40 },
	{ 0x9E, 0xC6 },
	{ 0x9F, 0x8E },
	{ 0xA2, 0x50 },
	{ 0xA3, 0xF0 },
	{ 0xAD, 0x08 },
	{ 0xBD, 0x4F },
	{ 0xAF, 0x54 },
	{ 0xB1, 0x08 },
	{ 0xB5, 0xAB },
	{ 0x1B, 0x2D },
	{ 0xB6, 0x4E },
	{ 0xB8, 0x70 },
	{ 0x2B, 0xB3 },
	{ 0x2C, 0x5D },
	{ 0x2D, 0x98 },
	{ 0x2E, 0xB0 },
	{ 0x2F, 0x20 },
	{ 0xA2, 0xD0 },
	{ 0x1D, 0x21 },
	{ 0x1E, 0xBE },
	{ 0x1C, 0x00 },
	{ 0x1D, 0x30 },
	{ 0x1E, 0x29 },
	{ 0x1C, 0x01 },
	{ 0x1D, 0x00 },
	{ 0x1E, 0x9E },
	{ 0x1C, 0x02 },
	{ 0x1D, 0x30 },
	{ 0x1E, 0xBB },
	{ 0x1C, 0x03 },
	{ 0x1D, 0x00 },
	{ 0x1E, 0x9D },
	{ 0x1C, 0x04 },
	{ 0x1D, 0x22 },
	{ 0x1E, 0xFF },
	{ 0x1C, 0x05 },
	{ 0x1D, 0x1B },
	{ 0x1E, 0x4E },
	{ 0x1C, 0x06 },
	{ 0x1D, 0x16 },
	{ 0x1E, 0x28 },
	{ 0x1C, 0x07 },
	{ 0x1D, 0x22 },
	{ 0x1E, 0xFF },
	{ 0x1C, 0x08 },
	{ 0x1D, 0x15 },
	{ 0x1E, 0xF1 },
	{ 0x1C, 0x09 },
	{ 0x1D, 0x30 },
	{ 0x1E, 0xD5 },
	{ 0x1C, 0x0A },
	{ 0x1D, 0x00 },
	{ 0x1E, 0x9E },
	{ 0x1C, 0x0B },
	{ 0x1D, 0x17 },
	{ 0x1E, 0x9D },
	{ 0x1C, 0x0C },
	{ 0x1D, 0x28 },
	{ 0x1E, 0xD7 },
	{ 0x1C, 0x0D },
	{ 0x1D, 0x17 },
	{ 0x1E, 0xD7 },
	{ 0x1C, 0x0E },
	{ 0x1D, 0x0A },
	{ 0x1E, 0xCB },
	{ 0x1C, 0x0F },
	{ 0x1D, 0x24 },
	{ 0x1E, 0x14 },
	{ 0x1C, 0x10 },
	{ 0x1D, 0x17 },
	{ 0x1E, 0x85 },
	{ 0x1C, 0x11 },
	{ 0x1D, 0x15 },
	{ 0x1E, 0x71 },
	{ 0x1C, 0x12 },
	{ 0x1D, 0x2B },
	{ 0x1E, 0x36 },
	{ 0x1C, 0x13 },
	{ 0x1D, 0x12 },
	{ 0x1E, 0x06 },
	{ 0x1C, 0x14 },
	{ 0x1D, 0x30 },
	{ 0x1E, 0x97 },
	{ 0x1C, 0x15 },
	{ 0x1D, 0x21 },
	{ 0x1E, 0x32 },
	{ 0x1C, 0x16 },
	{ 0x1D, 0x06 },
	{ 0x1E, 0xE6 },
	{ 0x1C, 0x17 },
	{ 0x1D, 0x16 },
	{ 0x1E, 0x06 },
	{ 0x1C, 0x18 },
	{ 0x1D, 0x30 },
	{ 0x1E, 0x01 },
	{ 0x1C, 0x19 },
	{ 0x1D, 0x21 },
	{ 0x1E, 0x37 },
	{ 0x1C, 0x1A },
	{ 0x1D, 0x00 },
	{ 0x1E, 0x08 },
	{ 0x1C, 0x1B },
	{ 0x1D, 0x80 },
	{ 0x1E, 0xD5 },
	{ 0xA2, 0x50 },
	{ 0xA2, 0x50 },
	{ 0x81, 0x01 }
};

static const struct aes_regwrite strip_scan_reqs[] = {
	{ 0xBE, 0x23 },
	{ 0x29, 0x06 },
	{ 0x2A, 0x35 },
	{ 0xBD, 0x4F },
	{ 0xFF, 0x00 }
};

static const struct aes_regwrite capture_stop[] = {
	{ 0x81,0x00 }
};

static int capture(struct fp_img_dev *dev, gboolean unconditional,
	struct fp_img **ret)
{
	int r;
	struct fp_img *img;
	unsigned int nstrips;
	unsigned int errors_sum, r_errors_sum;
	unsigned char *cooked;
	unsigned char *imgptr;
	unsigned char buf[665];
	int final_size;
	int sum;
	unsigned int count_blank = 0;
	int i;

	/* FIXME can do better here in terms of buffer management? */
	fp_dbg("");

	r = aes_write_regv(dev, capture_reqs, G_N_ELEMENTS(capture_reqs));
	if (r < 0)
		return r;
  
	/* FIXME: use histogram data above for gain calibration (0x8e xx) */

	img = fpi_img_new((3 * MAX_FRAMES * FRAME_SIZE) / 2);
	imgptr = img->data;
	cooked = imgptr + (MAX_FRAMES * FRAME_SIZE) / 2;

	r = read_data(dev, buf, 665);
	if (r < 0)
		goto err;
	memcpy(imgptr, buf + 1, 128*4);
	imgptr += 128*4;

	r = read_data(dev, buf, 665);
	if (r < 0)
		goto err;
	memcpy(imgptr, buf + 1, 128*4);
	imgptr += 128*4;

	/* we start at 2 because we captured 2 frames above. the above captures
	 * should possibly be moved into the loop below, or discarded altogether */
	for (nstrips = 2; nstrips < MAX_FRAMES - 2; nstrips++) {
		r = aes_write_regv(dev, strip_scan_reqs, G_N_ELEMENTS(strip_scan_reqs));
		if (r < 0)
			goto err;
		r = read_data(dev, buf, 665);
		if (r < 0)
			goto err;
		memcpy(imgptr, buf + 1, 128*4);
		imgptr += 128*4;

		r = read_data(dev, buf, 665);
		if (r < 0)
			goto err;
		memcpy(imgptr, buf + 1, 128*4);
		imgptr += 128*4;

		sum = 0;
		for (i = 515; i != 530; i++)
		{
			/* histogram[i] = number of pixels of value i
			   Only the pixel values from 10 to 15 are used to detect finger. */
			sum += buf[i];
		}
		if (sum < 0) {
			r = sum;
			goto err;
		}
		fp_dbg("sum=%d", sum);
		if (sum == 0)
			count_blank++;
		else
			count_blank = 0;
			
		/* if we got 50 blank frames, assume scan has ended. */
		if (count_blank >= 50)
			break;
	}
	
	r = aes_write_regv(dev, capture_stop, G_N_ELEMENTS(capture_stop));
	if (r < 0)
		goto err;
	r = read_data(dev, buf, 665);
	if (r < 0)
		goto err;
	memcpy(imgptr, buf + 1, 128*4);
	imgptr += 128*4;
	nstrips++;

	r = read_data(dev, buf, 665);
	if (r < 0)
		goto err;
	memcpy(imgptr, buf + 1, 128*4);
	imgptr += 128*4;
	nstrips++;
	
	if (nstrips == MAX_FRAMES)
		fp_warn("swiping finger too slow?");

	img->flags = FP_IMG_COLORS_INVERTED;
	img->height = assemble(img->data, cooked, nstrips, FALSE, &errors_sum);
	img->height = assemble(img->data, cooked, nstrips, TRUE, &r_errors_sum);
	
	if (r_errors_sum > errors_sum) {
		img->height = assemble(img->data, cooked, nstrips, FALSE, &errors_sum);
		img->flags |= FP_IMG_V_FLIPPED | FP_IMG_H_FLIPPED;
		fp_dbg("normal scan direction");
	} else {
		fp_dbg("reversed scan direction");
	}

	final_size = img->height * FRAME_WIDTH;
	memcpy(img->data, cooked, final_size);
	img = fpi_img_resize(img, final_size);
	*ret = img;
	return 0;
err:
	fp_img_free(img);
	return r;
}

static const struct usb_id id_table[] = {
	{ .vendor = 0x08ff, .product = 0x1600 },
	{ 0, 0, 0, },
};

struct fp_img_driver aes1610_driver = {
	.driver = {
		.id = 6,
		.name = FP_COMPONENT,
		.full_name = "AuthenTec AES1610",
		.id_table = id_table,
	},
	.flags = 0,
	.img_height = -1,
	.img_width = 128,

	/* temporarily lowered until we sort out image processing code
	 * binarized scan quality is good, minutiae detection is accurate,
	 * it's just that we get fewer minutiae than other scanners (less scanning
	 * area) */
	.bz3_threshold = 10,

	.init = dev_init,
	.exit = dev_exit,
	.await_finger_on = await_finger_on,
	.capture = capture,
};

