/*
 * AuthenTec AES2501 driver for libfprint
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

#define FP_COMPONENT "aes2501"

#include <errno.h>
#include <string.h>

#include <usb.h>

#include <aeslib.h>
#include <fp_internal.h>
#include "aes2501.h"

/* FIXME these need checking */
#define EP_IN			(1 | USB_ENDPOINT_IN)
#define EP_OUT			(2 | USB_ENDPOINT_OUT)

#define BULK_TIMEOUT 4000

/*
 * The AES2501 is an imaging device using a swipe-type sensor. It samples
 * the finger at preprogrammed intervals, sending a 192x16 frame to the
 * computer.
 * Unless the user is scanning their finger unreasonably fast, the frames
 * *will* overlap. The implementation below detects this overlap and produces
 * a contiguous image as the end result.
 * The fact that the user determines the length of the swipe (and hence the
 * number of useful frames) and also the fact that overlap varies means that
 * images returned from this driver vary in height.
 */

#define FRAME_WIDTH		192
#define FRAME_HEIGHT	16
#define FRAME_SIZE		(FRAME_WIDTH * FRAME_HEIGHT)
/* maximum number of frames to read during a scan */
/* FIXME reduce substantially */
#define MAX_FRAMES		150

static int read_data(struct fp_img_dev *dev, unsigned char *data, size_t len)
{
	int r;
	fp_dbg("len=%zd", len);

	r = usb_bulk_read(dev->udev, EP_IN, data, len, BULK_TIMEOUT);
	if (r < 0) {
		fp_err("bulk read error %d", r);
		return r;
	} else if (r < len) {
		fp_err("unexpected short read %d/%zd", r, len);
		return -EIO;
	}
	return 0;
}

static int read_regs(struct fp_img_dev *dev, unsigned char *data)
{
	int r;
	const struct aes_regwrite regwrite = {
		AES2501_REG_CTRL2, AES2501_CTRL2_READ_REGS
	};

	fp_dbg("");

	r = aes_write_regv(dev, &regwrite, 1);
	if (r < 0)
		return r;
	
	return read_data(dev, data, 126);
}

static const struct aes_regwrite init_1[] = {
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ 0, 0 },
	{ 0xb0, 0x27 }, /* Reserved? */
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ 0xff, 0x00 }, /* Reserved? */
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_DETCTRL,
		AES2501_DETCTRL_DRATE_CONTINUOUS | AES2501_DETCTRL_SDELAY_31_MS },
	{ AES2501_REG_COLSCAN, AES2501_COLSCAN_SRATE_128_US },
	{ AES2501_REG_MEASDRV,
		AES2501_MEASDRV_MDRIVE_0_325 | AES2501_MEASDRV_MEASURE_SQUARE },
	{ AES2501_REG_MEASFREQ, AES2501_MEASFREQ_2M },
	{ AES2501_REG_DEMODPHASE1, DEMODPHASE_NONE },
	{ AES2501_REG_DEMODPHASE2, DEMODPHASE_NONE },
	{ AES2501_REG_CHANGAIN,
		AES2501_CHANGAIN_STAGE2_4X | AES2501_CHANGAIN_STAGE1_16X },
	{ AES2501_REG_ADREFHI, 0x44 },
	{ AES2501_REG_ADREFLO, 0x34 },
	{ AES2501_REG_STRTCOL, 0x16 },
	{ AES2501_REG_ENDCOL, 0x16 },
	{ AES2501_REG_DATFMT, AES2501_DATFMT_BIN_IMG | 0x08 },
	{ AES2501_REG_TREG1, 0x70 },
	{ 0xa2, 0x02 },
	{ 0xa7, 0x00 },
	{ AES2501_REG_TREGC, AES2501_TREGC_ENABLE },
	{ AES2501_REG_TREGD, 0x1a },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_REG_UPDATE },
	{ AES2501_REG_CTRL2, AES2501_CTRL2_SET_ONE_SHOT },
	{ AES2501_REG_LPONT, AES2501_LPONT_MIN_VALUE },
};

static const struct aes_regwrite init_2[] = {
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_AUTOCALOFFSET, 0x41 },
	{ AES2501_REG_EXCITCTRL, 0x42 },
	{ AES2501_REG_DETCTRL, 0x53 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_REG_UPDATE },
};

static const struct aes_regwrite init_3[] = {
	{ 0xff, 0x00 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_AUTOCALOFFSET, 0x41 },
	{ AES2501_REG_EXCITCTRL, 0x42 },
	{ AES2501_REG_DETCTRL, 0x53 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_REG_UPDATE },
};

static const struct aes_regwrite init_4[] = {
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ 0xb0, 0x27 },
	{ AES2501_REG_ENDROW, 0x0a },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_REG_UPDATE },
	{ AES2501_REG_DETCTRL, 0x45 },
	{ AES2501_REG_AUTOCALOFFSET, 0x41 },
};

static const struct aes_regwrite init_5[] = {
	{ 0xb0, 0x27 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ 0xff, 0x00 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_SCAN_RESET },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_SCAN_RESET },
};

static int do_init(struct fp_img_dev *dev)
{
	unsigned char buffer[128];
	int r;
	int i;

	/* part 1, probably not needed */
	r = aes_write_regv(dev, init_1, G_N_ELEMENTS(init_1));
	if (r < 0)
		return r;

	r = read_data(dev, buffer, 20);
	if (r < 0)
		return r;

	/* part 2 */
	r = aes_write_regv(dev, init_2, G_N_ELEMENTS(init_2));
	if (r < 0)
		return r;

	r = read_regs(dev, buffer);
	if (r < 0)
		return r;

	/* part 3 */
	fp_dbg("reg 0xaf = %x", buffer[0x5f]);
	i = 0;
	while (buffer[0x5f] == 0x6b) {
		r = aes_write_regv(dev, init_3, G_N_ELEMENTS(init_3));
		if (r < 0)
			return r;
		r = read_regs(dev, buffer);
		if (r < 0)
			return r;
		if (++i == 13)
			break;
	}

	/* part 4 */
	r = aes_write_regv(dev, init_4, G_N_ELEMENTS(init_4));
	if (r < 0)
		return r;

	/* part 5 */
	return aes_write_regv(dev, init_5, G_N_ELEMENTS(init_5));
}

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	int r;

	r = usb_claim_interface(dev->udev, 0);
	if (r < 0) {
		fp_err("could not claim interface 0");
		return r;
	}

	/* FIXME check endpoints */

	return do_init(dev);
}

static void dev_exit(struct fp_img_dev *dev)
{
	usb_release_interface(dev->udev, 0);
}

static const struct aes_regwrite finger_det_reqs[] = {
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_DETCTRL,
		AES2501_DETCTRL_DRATE_CONTINUOUS | AES2501_DETCTRL_SDELAY_31_MS },
	{ AES2501_REG_COLSCAN, AES2501_COLSCAN_SRATE_128_US },
	{ AES2501_REG_MEASDRV, AES2501_MEASDRV_MDRIVE_0_325 | AES2501_MEASDRV_MEASURE_SQUARE },
	{ AES2501_REG_MEASFREQ, AES2501_MEASFREQ_2M },
	{ AES2501_REG_DEMODPHASE1, DEMODPHASE_NONE },
	{ AES2501_REG_DEMODPHASE2, DEMODPHASE_NONE },
	{ AES2501_REG_CHANGAIN,
		AES2501_CHANGAIN_STAGE2_4X | AES2501_CHANGAIN_STAGE1_16X },
	{ AES2501_REG_ADREFHI, 0x44 },
	{ AES2501_REG_ADREFLO, 0x34 },
	{ AES2501_REG_STRTCOL, 0x16 },
	{ AES2501_REG_ENDCOL, 0x16 },
	{ AES2501_REG_DATFMT, AES2501_DATFMT_BIN_IMG | 0x08 },
	{ AES2501_REG_TREG1, 0x70 },
	{ 0xa2, 0x02 },
	{ 0xa7, 0x00 },
	{ AES2501_REG_TREGC, AES2501_TREGC_ENABLE },
	{ AES2501_REG_TREGD, 0x1a },
	{ 0, 0 },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_REG_UPDATE },
	{ AES2501_REG_CTRL2, AES2501_CTRL2_SET_ONE_SHOT },
	{ AES2501_REG_LPONT, AES2501_LPONT_MIN_VALUE },
};

static int detect_finger(struct fp_img_dev *dev)
{
	unsigned char buffer[22];
	int r;
	int i;
	int sum = 0;

	r = aes_write_regv(dev, finger_det_reqs, G_N_ELEMENTS(finger_det_reqs));
	if (r < 0)
		return r;

	r = read_data(dev, buffer, 20);
	if (r < 0)
		return r;

	for (i = 1; i < 9; i++)
		sum += (buffer[i] & 0xf) + (buffer[i] >> 4);

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

/* Read the value of a specific register from a register dump */
static int regval_from_dump(unsigned char *data, uint8_t target)
{
	if (*data != FIRST_AES2501_REG) {
		fp_err("not a register dump");
		return -EILSEQ;
	}

	if (!(FIRST_AES2501_REG <= target && target <= LAST_AES2501_REG)) {
		fp_err("out of range");
		return -EINVAL;
	}

	target -= FIRST_AES2501_REG;
	target *= 2;
	return data[target + 1];
}

static int sum_histogram_values(unsigned char *data, uint8_t threshold)
{
	int r = 0;
	int i;
	uint16_t *histogram = (uint16_t *)(data + 1);

	if (*data != 0xde)
		return -EILSEQ;

	if (threshold > 0x0f)
		return -EINVAL;

	/* FIXME endianness */
	for (i = threshold; i < 16; i++)
		r += histogram[i];
	
	return r;
}

/* find overlapping parts of  frames */
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
	 * Taken from document describing aes2501 image format
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

static const struct aes_regwrite capture_reqs_1[] = {
	{ AES2501_REG_CTRL1, AES2501_CTRL1_MASTER_RESET },
	{ 0, 0 },
	{ AES2501_REG_EXCITCTRL, 0x40 },
	{ AES2501_REG_DETCTRL,
		AES2501_DETCTRL_SDELAY_31_MS | AES2501_DETCTRL_DRATE_CONTINUOUS },
	{ AES2501_REG_COLSCAN, AES2501_COLSCAN_SRATE_128_US },
	{ AES2501_REG_DEMODPHASE2, 0x7c },
	{ AES2501_REG_MEASDRV,
		AES2501_MEASDRV_MEASURE_SQUARE | AES2501_MEASDRV_MDRIVE_0_325 },
	{ AES2501_REG_DEMODPHASE1, 0x24 },
	{ AES2501_REG_CHWORD1, 0x00 },
	{ AES2501_REG_CHWORD2, 0x6c },
	{ AES2501_REG_CHWORD3, 0x09 },
	{ AES2501_REG_CHWORD4, 0x54 },
	{ AES2501_REG_CHWORD5, 0x78 },
	{ 0xa2, 0x02 },
	{ 0xa7, 0x00 },
	{ 0xb6, 0x26 },
	{ 0xb7, 0x1a },
	{ AES2501_REG_CTRL1, AES2501_CTRL1_REG_UPDATE },
	{ AES2501_REG_IMAGCTRL,
		AES2501_IMAGCTRL_TST_REG_ENABLE | AES2501_IMAGCTRL_HISTO_DATA_ENABLE |
		AES2501_IMAGCTRL_IMG_DATA_DISABLE },
	{ AES2501_REG_STRTCOL, 0x10 },
	{ AES2501_REG_ENDCOL, 0x1f },
	{ AES2501_REG_CHANGAIN,
		AES2501_CHANGAIN_STAGE1_2X | AES2501_CHANGAIN_STAGE2_2X },
	{ AES2501_REG_ADREFHI, 0x70 },
	{ AES2501_REG_ADREFLO, 0x20 },
	{ AES2501_REG_CTRL2, AES2501_CTRL2_SET_ONE_SHOT },
	{ AES2501_REG_LPONT, AES2501_LPONT_MIN_VALUE },
};

static const struct aes_regwrite capture_reqs_2[] = {
	{ AES2501_REG_IMAGCTRL,
		AES2501_IMAGCTRL_TST_REG_ENABLE | AES2501_IMAGCTRL_HISTO_DATA_ENABLE |
		AES2501_IMAGCTRL_IMG_DATA_DISABLE },
	{ AES2501_REG_STRTCOL, 0x10 },
	{ AES2501_REG_ENDCOL, 0x1f },
	{ AES2501_REG_CHANGAIN, AES2501_CHANGAIN_STAGE1_16X },
	{ AES2501_REG_ADREFHI, 0x70 },
	{ AES2501_REG_ADREFLO, 0x20 },
	{ AES2501_REG_CTRL2, AES2501_CTRL2_SET_ONE_SHOT },
};

static const struct aes_regwrite strip_scan_reqs[] = {
	{ AES2501_REG_IMAGCTRL,
		AES2501_IMAGCTRL_TST_REG_ENABLE | AES2501_IMAGCTRL_HISTO_DATA_ENABLE },
	{ AES2501_REG_STRTCOL, 0x00 },
	{ AES2501_REG_ENDCOL, 0x2f },
	{ AES2501_REG_CHANGAIN, AES2501_CHANGAIN_STAGE1_16X },
	{ AES2501_REG_ADREFHI, 0x5b },
	{ AES2501_REG_ADREFLO, 0x20 },
	{ AES2501_REG_CTRL2, AES2501_CTRL2_SET_ONE_SHOT },
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
	unsigned char buf[1705];
	int final_size;
	int sum;

	/* FIXME can do better here in terms of buffer management? */
	fp_dbg("");

	r = aes_write_regv(dev, capture_reqs_1, G_N_ELEMENTS(capture_reqs_1));
	if (r < 0)
		return r;

	r = read_data(dev, buf, 159);
	if (r < 0)
		return r;

	r = aes_write_regv(dev, capture_reqs_2, G_N_ELEMENTS(capture_reqs_2));
	if (r < 0)
		return r;

	r = read_data(dev, buf, 159);
	if (r < 0)
		return r;
	
	/* FIXME: use histogram data above for gain calibration (0x8e xx) */

	img = fpi_img_new((3 * MAX_FRAMES * FRAME_SIZE) / 2);
	imgptr = img->data;
	cooked = imgptr + (MAX_FRAMES * FRAME_SIZE) / 2;

	for (nstrips = 0; nstrips < MAX_FRAMES; nstrips++) {
		int threshold;

		r = aes_write_regv(dev, strip_scan_reqs, G_N_ELEMENTS(strip_scan_reqs));
		if (r < 0)
			goto err;
		r = read_data(dev, buf, 1705);
		if (r < 0)
			goto err;
		memcpy(imgptr, buf + 1, 192*8);
		imgptr += 192*8;

		threshold = regval_from_dump((buf + 1 + 192*8 + 1 + 16*2 + 1 + 8),
			AES2501_REG_DATFMT);
		if (threshold < 0) {
			r = threshold;
			goto err;
		}

	    sum = sum_histogram_values((buf + 1 + 192*8), threshold & 0x0f);
		if (sum < 0) {
			r = sum;
			goto err;
		}
		fp_dbg("sum=%d", sum);
		if (sum == 0)
			break;
	}
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
	{ .vendor = 0x08ff, .product = 0x2580 },
	{ 0, 0, 0, },
};

struct fp_img_driver aes2501_driver = {
	.driver = {
		.id = 4,
		.name = FP_COMPONENT,
		.full_name = "AuthenTec AES2501",
		.id_table = id_table,
	},
	.flags = 0,
	.img_height = -1,
	.img_width = 192,

	.init = dev_init,
	.exit = dev_exit,
	.await_finger_on = await_finger_on,
	.capture = capture,
};

