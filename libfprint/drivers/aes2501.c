/*
 * AuthenTec AES2501 driver for libfprint
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2007 Cyrille Bagard
 * Copyright (C) 2007-2008, 2012 Vasily Khoruzhick <anarsoul@gmail.com>
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

#include "drivers_api.h"
#include "aeslib.h"
#include "aes2501.h"

static void start_capture(struct fp_img_dev *dev);
static void complete_deactivation(struct fp_img_dev *dev);

/* FIXME these need checking */
#define EP_IN			(1 | LIBUSB_ENDPOINT_IN)
#define EP_OUT			(2 | LIBUSB_ENDPOINT_OUT)

#define BULK_TIMEOUT 4000

#define FINGER_DETECTION_LEN	20
#define READ_REGS_LEN		126
#define READ_REGS_RESP_LEN	159
#define STRIP_CAPTURE_LEN	1705

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
#define IMAGE_WIDTH		(FRAME_WIDTH + (FRAME_WIDTH / 2))
/* maximum number of frames to read during a scan */
/* FIXME reduce substantially */
#define MAX_FRAMES		150

/****** GENERAL FUNCTIONS ******/

struct aes2501_dev {
	uint8_t read_regs_retry_count;
	GSList *strips;
	size_t strips_len;
	gboolean deactivating;
	int no_finger_cnt;
};

static struct fpi_frame_asmbl_ctx assembling_ctx = {
	.frame_width = FRAME_WIDTH,
	.frame_height = FRAME_HEIGHT,
	.image_width = IMAGE_WIDTH,
	.get_pixel = aes_get_pixel,
};

typedef void (*aes2501_read_regs_cb)(struct fp_img_dev *dev, int status,
	unsigned char *regs, void *user_data);

struct aes2501_read_regs {
	struct fp_img_dev *dev;
	aes2501_read_regs_cb callback;
	struct aes_regwrite *regwrite;
	void *user_data;
};

static void read_regs_data_cb(struct libusb_transfer *transfer,
			      struct fp_dev          *dev,
			      fpi_ssm                *ssm,
			      void                   *user_data)
{
	struct aes2501_read_regs *rdata = user_data;
	unsigned char *retdata = NULL;
	int r;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		r = -EIO;
	} else if (transfer->length != transfer->actual_length) {
		r = -EPROTO;
	} else {
		r = 0;
		retdata = transfer->buffer;
	}

	rdata->callback(rdata->dev, r, retdata, rdata->user_data);
	g_free(rdata);
}

static void read_regs_rq_cb(struct fp_img_dev *dev, int result, void *user_data)
{
	struct aes2501_read_regs *rdata = user_data;
	fpi_usb_transfer *transfer;
	unsigned char *data;
	int r;

	g_free(rdata->regwrite);
	if (result != 0)
		goto err;

	data = g_malloc(READ_REGS_LEN);
	transfer = fpi_usb_fill_bulk_transfer(FP_DEV(dev),
					      NULL,
					      EP_IN,
					      data,
					      READ_REGS_LEN,
					      read_regs_data_cb,
					      rdata,
					      BULK_TIMEOUT);

	r = fpi_usb_submit_transfer(transfer);
	if (r < 0) {
		result = -EIO;
		goto err;
	}

	return;
err:
	rdata->callback(dev, result, NULL, rdata->user_data);
	g_free(rdata);
}

static void read_regs(struct fp_img_dev *dev, aes2501_read_regs_cb callback,
	void *user_data)
{
	/* FIXME: regwrite is dynamic because of asynchronity. is this really
	 * required? */
	struct aes_regwrite *regwrite = g_malloc(sizeof(*regwrite));
	struct aes2501_read_regs *rdata = g_malloc(sizeof(*rdata));

	G_DEBUG_HERE();
	regwrite->reg = AES2501_REG_CTRL2;
	regwrite->value = AES2501_CTRL2_READ_REGS;
	rdata->dev = dev;
	rdata->callback = callback;
	rdata->user_data = user_data;
	rdata->regwrite = regwrite;

	aes_write_regv(dev, (const struct aes_regwrite *) regwrite, 1,
		read_regs_rq_cb, rdata);
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

static void generic_write_regv_cb(struct fp_img_dev *dev, int result,
	void *user_data)
{
	fpi_ssm *ssm = user_data;
	if (result == 0)
		fpi_ssm_next_state(ssm);
	else
		fpi_ssm_mark_failed(ssm, result);
}

/* check that read succeeded but ignore all data */
static void generic_ignore_data_cb(struct libusb_transfer *transfer,
				   struct fp_dev          *dev,
				   fpi_ssm                *ssm,
				   void                   *user_data)
{
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
		fpi_ssm_mark_failed(ssm, -EIO);
	else if (transfer->length != transfer->actual_length)
		fpi_ssm_mark_failed(ssm, -EPROTO);
	else
		fpi_ssm_next_state(ssm);
}

/* read the specified number of bytes from the IN endpoint but throw them
 * away, then increment the SSM */
static void generic_read_ignore_data(fpi_ssm *ssm, struct fp_dev *dev, size_t bytes)
{
	fpi_usb_transfer *transfer;
	unsigned char *data;
	int r;

	data = g_malloc(bytes);
	transfer = fpi_usb_fill_bulk_transfer(dev,
					      ssm,
					      EP_IN,
					      data,
					      bytes,
					      generic_ignore_data_cb,
					      NULL,
					      BULK_TIMEOUT);

	r = fpi_usb_submit_transfer(transfer);
	if (r < 0)
		fpi_ssm_mark_failed(ssm, r);
}

/****** IMAGE PROCESSING ******/

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

/****** FINGER PRESENCE DETECTION ******/

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

static void start_finger_detection(struct fp_img_dev *dev);

static void finger_det_data_cb(struct libusb_transfer *transfer,
			       struct fp_dev          *_dev,
			       fpi_ssm                *ssm,
			       void                   *user_data)
{
	struct fp_img_dev *dev = FP_IMG_DEV(_dev);
	unsigned char *data = transfer->buffer;
	int i;
	int sum = 0;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fpi_imgdev_session_error(dev, -EIO);
		return;
	} else if (transfer->length != transfer->actual_length) {
		fpi_imgdev_session_error(dev, -EPROTO);
		return;
	}

	/* examine histogram to determine finger presence */
	for (i = 1; i < 9; i++)
		sum += (data[i] & 0xf) + (data[i] >> 4);
	if (sum > 20) {
		/* finger present, start capturing */
		fpi_imgdev_report_finger_status(dev, TRUE);
		start_capture(dev);
	} else {
		/* no finger, poll for a new histogram */
		start_finger_detection(dev);
	}
}

static void finger_det_reqs_cb(struct fp_img_dev *dev, int result,
	void *user_data)
{
	fpi_usb_transfer *transfer;
	unsigned char *data;
	int r;

	if (result) {
		fpi_imgdev_session_error(dev, result);
		return;
	}

	data = g_malloc(FINGER_DETECTION_LEN);
	transfer = fpi_usb_fill_bulk_transfer(FP_DEV(dev),
					      NULL,
					      EP_IN,
					      data,
					      FINGER_DETECTION_LEN,
					      finger_det_data_cb,
					      NULL,
					      BULK_TIMEOUT);

	r = fpi_usb_submit_transfer(transfer);
	if (r < 0)
		fpi_imgdev_session_error(dev, r);
}

static void start_finger_detection(struct fp_img_dev *dev)
{
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	G_DEBUG_HERE();

	if (aesdev->deactivating) {
		complete_deactivation(dev);
		return;
	}

	aes_write_regv(dev, finger_det_reqs, G_N_ELEMENTS(finger_det_reqs),
		finger_det_reqs_cb, NULL);
}

/****** CAPTURE ******/

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

static struct aes_regwrite strip_scan_reqs[] = {
	{ AES2501_REG_IMAGCTRL,
		AES2501_IMAGCTRL_TST_REG_ENABLE | AES2501_IMAGCTRL_HISTO_DATA_ENABLE },
	{ AES2501_REG_STRTCOL, 0x00 },
	{ AES2501_REG_ENDCOL, 0x2f },
	{ AES2501_REG_CHANGAIN, AES2501_CHANGAIN_STAGE1_16X },
	{ AES2501_REG_ADREFHI, AES2501_ADREFHI_MAX_VALUE },
	{ AES2501_REG_ADREFLO, 0x20 },
	{ AES2501_REG_CTRL2, AES2501_CTRL2_SET_ONE_SHOT },
};

/* capture SM movement:
 * write reqs and read data 1 + 2,
 * request and read strip,
 * jump back to request UNLESS theres no finger, in which case exit SM,
 * report lack of finger presence, and move to finger detection */

enum capture_states {
	CAPTURE_WRITE_REQS_1,
	CAPTURE_READ_DATA_1,
	CAPTURE_WRITE_REQS_2,
	CAPTURE_READ_DATA_2,
	CAPTURE_REQUEST_STRIP,
	CAPTURE_READ_STRIP,
	CAPTURE_NUM_STATES,
};

static void capture_read_strip_cb(struct libusb_transfer *transfer,
				  struct fp_dev          *_dev,
				  fpi_ssm                *ssm,
				  void                   *user_data)
{
	unsigned char *stripdata;
	struct fp_img_dev *dev = FP_IMG_DEV(_dev);
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(_dev);
	unsigned char *data = transfer->buffer;
	int sum;
	int threshold;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fpi_ssm_mark_failed(ssm, -EIO);
		return;
	} else if (transfer->length != transfer->actual_length) {
		fpi_ssm_mark_failed(ssm, -EPROTO);
		return;
	}

	threshold = regval_from_dump(data + 1 + 192*8 + 1 + 16*2 + 1 + 8,
		AES2501_REG_DATFMT);
	if (threshold < 0) {
		fpi_ssm_mark_failed(ssm, threshold);
		return;
	}

	sum = sum_histogram_values(data + 1 + 192*8, threshold & 0x0f);
	if (sum < 0) {
		fpi_ssm_mark_failed(ssm, sum);
		return;
	}
	fp_dbg("sum=%d", sum);

	if (sum < AES2501_SUM_LOW_THRESH) {
		strip_scan_reqs[4].value -= 0x8;
		if (strip_scan_reqs[4].value < AES2501_ADREFHI_MIN_VALUE)
			strip_scan_reqs[4].value = AES2501_ADREFHI_MIN_VALUE;
	} else if (sum > AES2501_SUM_HIGH_THRESH) {
		strip_scan_reqs[4].value += 0x8;
		if (strip_scan_reqs[4].value > AES2501_ADREFHI_MAX_VALUE)
			strip_scan_reqs[4].value = AES2501_ADREFHI_MAX_VALUE;
	}
	fp_dbg("ADREFHI is %.2x", strip_scan_reqs[4].value);

	/* Sum is 0, maybe finger was removed? Wait for 3 empty frames
	 * to ensure
	 */
	if (sum == 0) {
		aesdev->no_finger_cnt++;
		if (aesdev->no_finger_cnt == 3) {
			struct fp_img *img;

			aesdev->strips = g_slist_reverse(aesdev->strips);
			fpi_do_movement_estimation(&assembling_ctx,
					aesdev->strips, aesdev->strips_len);
			img = fpi_assemble_frames(&assembling_ctx,
						  aesdev->strips, aesdev->strips_len);
			img->flags |= FP_IMG_PARTIAL;
			g_slist_free_full(aesdev->strips, g_free);
			aesdev->strips = NULL;
			aesdev->strips_len = 0;
			fpi_imgdev_image_captured(dev, img);
			fpi_imgdev_report_finger_status(dev, FALSE);
			/* marking machine complete will re-trigger finger detection loop */
			fpi_ssm_mark_completed(ssm);
		} else {
			fpi_ssm_jump_to_state(ssm, CAPTURE_REQUEST_STRIP);
		}
	} else {
		/* obtain next strip */
		/* FIXME: would preallocating strip buffers be a decent optimization? */
		struct fpi_frame *stripe = g_malloc(FRAME_WIDTH * FRAME_HEIGHT / 2 + sizeof(struct fpi_frame));
		stripe->delta_x = 0;
		stripe->delta_y = 0;
		stripdata = stripe->data;
		memcpy(stripdata, data + 1, 192*8);
		aesdev->no_finger_cnt = 0;
		aesdev->strips = g_slist_prepend(aesdev->strips, stripe);
		aesdev->strips_len++;

		fpi_ssm_jump_to_state(ssm, CAPTURE_REQUEST_STRIP);
	}
}

static void capture_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(_dev);
	int r;

	switch (fpi_ssm_get_cur_state(ssm)) {
	case CAPTURE_WRITE_REQS_1:
		aes_write_regv(dev, capture_reqs_1, G_N_ELEMENTS(capture_reqs_1),
			generic_write_regv_cb, ssm);
		break;
	case CAPTURE_READ_DATA_1:
		generic_read_ignore_data(ssm, _dev, READ_REGS_RESP_LEN);
		break;
	case CAPTURE_WRITE_REQS_2:
		aes_write_regv(dev, capture_reqs_2, G_N_ELEMENTS(capture_reqs_2),
			generic_write_regv_cb, ssm);
		break;
	case CAPTURE_READ_DATA_2:
		generic_read_ignore_data(ssm, _dev, READ_REGS_RESP_LEN);
		break;
	case CAPTURE_REQUEST_STRIP:
		if (aesdev->deactivating)
			fpi_ssm_mark_completed(ssm);
		else
			aes_write_regv(dev, strip_scan_reqs, G_N_ELEMENTS(strip_scan_reqs),
				generic_write_regv_cb, ssm);
		break;
	case CAPTURE_READ_STRIP: ;
		fpi_usb_transfer *transfer;
		unsigned char *data;

		data = g_malloc(STRIP_CAPTURE_LEN);
		transfer = fpi_usb_fill_bulk_transfer(FP_DEV(dev),
						      ssm,
						      EP_IN,
						      data,
						      STRIP_CAPTURE_LEN,
						      capture_read_strip_cb,
						      NULL,
						      BULK_TIMEOUT);

		r = fpi_usb_submit_transfer(transfer);
		if (r < 0)
			fpi_ssm_mark_failed(ssm, r);
		break;
	};
}

static void capture_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(_dev);

	G_DEBUG_HERE();
	if (aesdev->deactivating)
		complete_deactivation(dev);
	else if (fpi_ssm_get_error(ssm))
		fpi_imgdev_session_error(dev, fpi_ssm_get_error(ssm));
	else
		start_finger_detection(dev);
	fpi_ssm_free(ssm);
}

static void start_capture(struct fp_img_dev *dev)
{
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_ssm *ssm;

	if (aesdev->deactivating) {
		complete_deactivation(dev);
		return;
	}

	aesdev->no_finger_cnt = 0;
	/* Reset gain */
	strip_scan_reqs[4].value = AES2501_ADREFHI_MAX_VALUE;
	ssm = fpi_ssm_new(FP_DEV(dev), capture_run_state, CAPTURE_NUM_STATES, dev);
	G_DEBUG_HERE();
	fpi_ssm_start(ssm, capture_sm_complete);
}

/****** INITIALIZATION/DEINITIALIZATION ******/

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

enum activate_states {
	WRITE_INIT_1,
	READ_DATA_1,
	WRITE_INIT_2,
	READ_REGS,
	WRITE_INIT_3,
	WRITE_INIT_4,
	WRITE_INIT_5,
	ACTIVATE_NUM_STATES,
};

void activate_read_regs_cb(struct fp_img_dev *dev, int status,
	unsigned char *regs, void *user_data)
{
	fpi_ssm *ssm = user_data;
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));

	if (status != 0) {
		fpi_ssm_mark_failed(ssm, status);
	} else {
		fp_dbg("reg 0xaf = %x", regs[0x5f]);
		if (regs[0x5f] != 0x6b || ++aesdev->read_regs_retry_count == 13)
			fpi_ssm_jump_to_state(ssm, WRITE_INIT_4);
		else
			fpi_ssm_next_state(ssm);
	}
}

static void activate_init3_cb(struct fp_img_dev *dev, int result,
	void *user_data)
{
	fpi_ssm *ssm = user_data;
	if (result == 0)
		fpi_ssm_jump_to_state(ssm, READ_REGS);
	else
		fpi_ssm_mark_failed(ssm, result);
}

static void activate_run_state(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;

	/* This state machine isn't as linear as it may appear. After doing init1
	 * and init2 register configuration writes, we have to poll a register
	 * waiting for a specific value. READ_REGS checks the register value, and
	 * if we're ready to move on, we jump to init4. Otherwise, we write init3
	 * and then jump back to READ_REGS. In a synchronous model:

	   [...]
	   aes_write_regv(init_2);
	   read_regs(into buffer);
	   i = 0;
	   while (buffer[0x5f] == 0x6b) {
	       aes_write_regv(init_3);
		   read_regs(into buffer);
	       if (++i == 13)
	           break;
	   }
	   aes_write_regv(init_4);
	*/

	switch (fpi_ssm_get_cur_state(ssm)) {
	case WRITE_INIT_1:
		aes_write_regv(dev, init_1, G_N_ELEMENTS(init_1),
			generic_write_regv_cb, ssm);
		break;
	case READ_DATA_1:
		fp_dbg("read data 1");
		generic_read_ignore_data(ssm, _dev, FINGER_DETECTION_LEN);
		break;
	case WRITE_INIT_2:
		aes_write_regv(dev, init_2, G_N_ELEMENTS(init_2),
			generic_write_regv_cb, ssm);
		break;
	case READ_REGS:
		read_regs(dev, activate_read_regs_cb, ssm);
		break;
	case WRITE_INIT_3:
		aes_write_regv(dev, init_3, G_N_ELEMENTS(init_3),
			activate_init3_cb, ssm);
		break;
	case WRITE_INIT_4:
		aes_write_regv(dev, init_4, G_N_ELEMENTS(init_4),
			generic_write_regv_cb, ssm);
		break;
	case WRITE_INIT_5:
		aes_write_regv(dev, init_5, G_N_ELEMENTS(init_5),
			generic_write_regv_cb, ssm);
		break;
	}
}

static void activate_sm_complete(fpi_ssm *ssm, struct fp_dev *_dev, void *user_data)
{
	struct fp_img_dev *dev = user_data;
	fp_dbg("status %d", fpi_ssm_get_error(ssm));
	fpi_imgdev_activate_complete(dev, fpi_ssm_get_error(ssm));

	if (!fpi_ssm_get_error(ssm))
		start_finger_detection(dev);
	fpi_ssm_free(ssm);
}

static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	fpi_ssm *ssm = fpi_ssm_new(FP_DEV(dev), activate_run_state,
		ACTIVATE_NUM_STATES, dev);
	aesdev->read_regs_retry_count = 0;
	fpi_ssm_start(ssm, activate_sm_complete);
	return 0;
}

static void dev_deactivate(struct fp_img_dev *dev)
{
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	/* FIXME: audit cancellation points, probably need more, specifically
	 * in error handling paths? */
	aesdev->deactivating = TRUE;
}

static void complete_deactivation(struct fp_img_dev *dev)
{
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	G_DEBUG_HERE();

	/* FIXME: if we're in the middle of a scan, we should cancel the scan.
	 * maybe we can do this with a master reset, unconditionally? */

	aesdev->deactivating = FALSE;
	g_slist_free(aesdev->strips);
	aesdev->strips = NULL;
	aesdev->strips_len = 0;
	fpi_imgdev_deactivate_complete(dev);
}

static int dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	/* FIXME check endpoints */
	int r;
	struct aes2501_dev *aesdev;

	r = libusb_claim_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	if (r < 0) {
		fp_err("could not claim interface 0: %s", libusb_error_name(r));
		return r;
	}

	aesdev = g_malloc0(sizeof(struct aes2501_dev));
	fp_dev_set_instance_data(FP_DEV(dev), aesdev);
	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

static void dev_deinit(struct fp_img_dev *dev)
{
	struct aes2501_dev *aesdev = FP_INSTANCE_DATA(FP_DEV(dev));
	g_free(aesdev);
	libusb_release_interface(fpi_dev_get_usb_dev(FP_DEV(dev)), 0);
	fpi_imgdev_close_complete(dev);
}

static const struct usb_id id_table[] = {
	{ .vendor = 0x08ff, .product = 0x2500 }, /* AES2500 */
	{ .vendor = 0x08ff, .product = 0x2580 }, /* AES2501 */
	{ 0, 0, 0, },
};

struct fp_img_driver aes2501_driver = {
	.driver = {
		.id = AES2501_ID,
		.name = FP_COMPONENT,
		.full_name = "AuthenTec AES2501",
		.id_table = id_table,
		.scan_type = FP_SCAN_TYPE_SWIPE,
	},
	.flags = 0,
	.img_height = -1,
	.img_width = IMAGE_WIDTH,

	.open = dev_init,
	.close = dev_deinit,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
};

