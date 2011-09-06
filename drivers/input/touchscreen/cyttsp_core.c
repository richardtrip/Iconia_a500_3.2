/*
 * Core Source for:
 * Cypress TrueTouch(TM) Standard Product (TTSP) touchscreen drivers.
 * For use with Cypress Txx3xx parts.
 * Supported parts include:
 * CY8CTST341
 * CY8CTMA340
 *
 * Copyright (C) 2009, 2010, 2011 Cypress Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contact Cypress Semiconductor at www.cypress.com <kev@cypress.com>
 *
 */
/* set this define to enable dev_dbg
#define DEBUG
 */
#include "cyttsp_core.h"

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/earlysuspend.h>

/* Bootloader File 0 offset */
#define CY_BL_FILE0       0x00
/* Bootloader command directive */
#define CY_BL_CMD         0xFF
/* Bootloader Exit and Verify Checksum command */
#define CY_BL_EXIT        0xA5
/* Bootloader number of command keys */
#define CY_NUM_BL_KEYS    8
/* Bootloader default command keys */
#define CY_BL_KEY0 0
#define CY_BL_KEY1 1
#define CY_BL_KEY2 2
#define CY_BL_KEY3 3
#define CY_BL_KEY4 4
#define CY_BL_KEY5 5
#define CY_BL_KEY6 6
#define CY_BL_KEY7 7

/* helpers */
#define GET_NUM_TOUCHES(x)          ((x) & 0x0F)
#define IS_LARGE_AREA(x)            (((x) & 0x10) >> 4)
#define IS_BAD_PKT(x)               ((x) & 0x20)
#define IS_VALID_APP(x)             ((x) & 0x01)
#define IS_OPERATIONAL_ERR(x)       ((x) & 0x3F)
#define GET_HSTMODE(reg)            ((reg & 0x70) >> 4)
#define GET_BOOTLOADERMODE(reg)     ((reg & 0x10) >> 4)

/* maximum number of concurrent tracks */
#define CY_NUM_TCH_ID               10
/* maximum number of track IDs */
#define CY_NUM_TRK_ID               16

#define CY_NTCH                     0 /* lift off */
#define CY_TCH                      1 /* touch down */
#define CY_SMALL_TOOL_WIDTH         10
#define CY_LARGE_TOOL_WIDTH         255
#define CY_REG_BASE                 0x00
#define CY_REG_ACT_DIST             0x1E
#define CY_REG_ACT_INTRVL           0x1D
#define CY_REG_TCH_TMOUT            (CY_REG_ACT_INTRVL+1)
#define CY_REG_LP_INTRVL            (CY_REG_TCH_TMOUT+1)
#define CY_MAXZ                     255
#define CY_DELAY_DFLT               200 /* ms */
#define CY_DELAY_MAX                (500/CY_DELAY_DFLT) /* half second */
#define CY_ACT_DIST_DFLT            0xF8
#define CY_HNDSHK_BIT               0x80
/* device mode bits */
#define CY_OPERATE_MODE             0x00 /* write to hst_mode_reg */
#define CY_SYSINFO_MODE             0x10 /* write to hst_mode reg */
#define CY_BL_MODE                  0X10 /* write to tt_mode reg */
/* power mode select bits */
#define CY_SOFT_RESET_MODE          0x01 /* return to Bootloader mode */
#define CY_DEEP_SLEEP_MODE          0x02
#define CY_LOW_POWER_MODE           0x04

#define CY_HOR_OST		    10 /* add this; set this to proper offset value */
#define TEGRA_GPIO_PQ7    135
#define SYSINFO           0

struct cyttsp *g_ts;

/* Touch structure */
struct cyttsp_touch {
	u8 x[2];
	u8 y[2];
	u8 z;
};

/* TrueTouch Standard Product Gen3 interface definition */
struct cyttsp_xydata_gen3 {
	u8 hst_mode;
	u8 tt_mode;
	u8 tt_stat;
	struct cyttsp_touch tch1;
	u8 touch12_id;
	struct cyttsp_touch tch2;
	u8 gest_cnt;
	u8 gest_id;
	struct cyttsp_touch tch3;
	u8 touch34_id;
	struct cyttsp_touch tch4;
	u8 tt_undef[3];
	u8 act_dist;
	u8 tt_reserved;
};

/* TrueTouch Standard Product Gen4 (Txx4xx) interface definition */
struct cyttsp_xydata_ext {
	struct cyttsp_touch tch5;
	u8 touch56_id;
	struct cyttsp_touch tch6;
	struct cyttsp_touch tch7;
	u8 touch78_id;
	struct cyttsp_touch tch8;
	struct cyttsp_touch tch9;
	u8 touch910_id;
	struct cyttsp_touch tch10;
};

struct cyttsp_xydata_gen4 {
	struct cyttsp_xydata_gen3 regs;
	struct cyttsp_xydata_ext xregs;
};

/* TTSP System Information interface definition */
struct cyttsp_sysinfo_data {
	u8 hst_mode;
	u8 mfg_cmd;
	u8 mfg_stat;
	u8 cid[3];
	u8 tt_undef1;
	u8 uid[8];
	u8 bl_verh;
	u8 bl_verl;
	u8 tts_verh;
	u8 tts_verl;
	u8 app_idh;
	u8 app_idl;
	u8 app_verh;
	u8 app_verl;
	u8 tt_undef[5];
	u8 scn_typ;
	u8 act_intrvl;
	u8 tch_tmout;
	u8 lp_intrvl;
};

/* TTSP Bootloader Register Map interface definition */
#define CY_BL_CHKSUM_OK 0x01
struct cyttsp_bootloader_data {
	u8 bl_file;
	u8 bl_status;
	u8 bl_error;
	u8 blver_hi;
	u8 blver_lo;
	u8 bld_blver_hi;
	u8 bld_blver_lo;
	u8 lstsver_hi;
	u8 lstsver_lo;
	u8 appid_hi;
	u8 appid_lo;
	u8 appver_hi;
	u8 appver_lo;
	u8 cid_0;
	u8 cid_1;
	u8 cid_2;
};

struct cyttsp_tch {
	struct cyttsp_touch *tch;
	u8 *id;
};

struct cyttsp_trk {
	bool tch;
	u16 x;
	u16 y;
	u8 z;
};

struct cyttsp {
	struct device *dev;
	int irq;
	struct input_dev *input;
	struct mutex mutex;
	char phys[32];
	const struct bus_type *bus_type;
	const struct cyttsp_platform_data *platform_data;
	struct cyttsp_bus_ops *bus_ops;
	struct cyttsp_xydata_gen4 xy_data;
	struct cyttsp_bootloader_data bl_data;
	struct cyttsp_sysinfo_data sysinfo_data;
	struct cyttsp_trk prv_trk[CY_NUM_TRK_ID];
	struct cyttsp_tch tch_map[CY_NUM_TCH_ID];
	struct timer_list to_timer;
	struct completion bl_ready;
	enum cyttsp_powerstate power_state;
	bool powered;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
	unsigned suspended;
#endif
#ifdef CONFIG_TOUCHSCREEN_CYLSTS_FBL
	void *fw_fbl;
#endif
};

struct cyttsp_track_data {
	struct cyttsp_trk cur_trk[CY_NUM_TRK_ID];
};

static const u8 bl_command[] = {
	CY_BL_FILE0, CY_BL_CMD, CY_BL_EXIT,
	CY_BL_KEY0, CY_BL_KEY1, CY_BL_KEY2,
	CY_BL_KEY3, CY_BL_KEY4, CY_BL_KEY5,
	CY_BL_KEY6, CY_BL_KEY7
};

static int ttsp_read_block_data(struct cyttsp *ts, u16 command,
	u8 length, void *buf, int i2c_addr, bool use_long_subaddr)
{
	int retval;
	int tries;

	if (!buf || !length)
		return -EIO;

	for (tries = 0, retval = -1;
		tries < CY_NUM_RETRY && (retval < 0);
		tries++) {
		retval = ts->bus_ops->read(ts->bus_ops, command, length, buf,
			i2c_addr, use_long_subaddr);
		if (retval)
			msleep(CY_DELAY_DFLT);
	}

	return retval;
}

static int ttsp_write_block_data(struct cyttsp *ts, u16 command,
	u8 length, void *buf, int i2c_addr, bool use_long_subaddr)
{
	int retval;
	int tries;

	if (!buf || !length)
		return -EIO;

	for (tries = 0, retval = -1;
		tries < CY_NUM_RETRY && (retval < 0);
		tries++) {
		retval = ts->bus_ops->write(ts->bus_ops, command, length, buf,
			i2c_addr, use_long_subaddr);
		if (retval)
			msleep(CY_DELAY_DFLT);
	}

	return retval;
}

static int ttsp_tch_ext(struct cyttsp *ts, void *buf)
{
	int retval;

	if (!buf)
		return -EIO;

	retval = ts->bus_ops->ext(ts->bus_ops, buf);

	return retval;
}

static int cyttsp_enter_bl_mode(struct cyttsp *ts)
{
	u8 enter_bl_cmd = CY_BL_MODE;
	int tries;
	int retval;
	tries = 0;
	retval = ttsp_write_block_data(ts, CY_REG_BASE + 1,
		sizeof(enter_bl_cmd), &enter_bl_cmd, 0x67, true);
	mdelay(1000);
	dev_dbg(ts->dev, "%s: tries=%d ret=%d\n",
		__func__, tries, retval);
	return retval;
}

static int cyttsp_exit_bl_mode(struct cyttsp *ts)
{
	u8 exit_bl_cmd[] = {0x01, 0x3B, 0x00, 0x00, 0xC4, 0xFF, 0x17};
	int tries;
	int retval;

	tries = 0;
	do {
		mdelay(CY_DELAY_DFLT);
		retval = ttsp_write_block_data(ts, CY_REG_BASE,
			sizeof(exit_bl_cmd), (u8 *)exit_bl_cmd, 0x6A, false);
	} while ((retval < 0) && (tries++ < CY_DELAY_MAX));

	dev_dbg(ts->dev, "%s: tries=%d ret=%d\n",
		__func__, tries, retval);

	/* wait for TTSP Device to complete switch to Operational mode */
	mdelay(20);
	return retval;
}

static void cyttsp_set_operational_mode(struct cyttsp *ts)
{
	int retval;
	u8 cmd = CY_OPERATE_MODE;

	retval = ttsp_write_block_data(ts, CY_REG_BASE,
		sizeof(cmd), &cmd, 0x67, true);

	if (retval < 0)
		printk("%s error!!\n", __func__);
}

#if SYSINFO
static int cyttsp_set_sysinfo_mode(struct cyttsp *ts)
{
	int retval;
	int tries;
	u8 cmd = CY_SYSINFO_MODE;

	memset(&(ts->sysinfo_data), 0, sizeof(struct cyttsp_sysinfo_data));

	/* switch to sysinfo mode */
	retval = ttsp_write_block_data(ts, CY_REG_BASE,
		sizeof(cmd), &cmd, 0x67, true);
	if (retval < 0)
		return retval;

	/* read sysinfo registers */
	tries = 0;
	do {
		msleep(CY_DELAY_DFLT);
		retval = ttsp_read_block_data(ts, CY_REG_BASE,
			sizeof(ts->sysinfo_data), &(ts->sysinfo_data),
			0x67, true);
	} while (!((retval == 0) &&
		!((ts->sysinfo_data.tts_verh == 0) &&
		(ts->sysinfo_data.tts_verl == 0))) &&
		(tries++ < CY_DELAY_MAX));

	dev_dbg(ts->dev, "%s: check sysinfo ready tries=%d ret=%d\n",
		__func__, tries, retval);
	dev_info(ts->dev, "%s: tv=%02X%02X ai=0x%02X%02X "
		"av=0x%02X%02X ci=0x%02X%02X%02X\n", "cyttsp",
		ts->sysinfo_data.tts_verh, ts->sysinfo_data.tts_verl,
		ts->sysinfo_data.app_idh, ts->sysinfo_data.app_idl,
		ts->sysinfo_data.app_verh, ts->sysinfo_data.app_verl,
		ts->sysinfo_data.cid[0], ts->sysinfo_data.cid[1],
		ts->sysinfo_data.cid[2]);

	return retval;
}

static int cyttsp_act_dist_setup(struct cyttsp *ts)
{
	int retval;
	u8 act_dist_setup;

	/* Init gesture; active distance setup */
	act_dist_setup = ts->platform_data->act_dist;
	retval = ttsp_write_block_data(ts, CY_REG_ACT_DIST,
		sizeof(act_dist_setup), &act_dist_setup, 0x67, true);

	return retval;
}
#endif

/* map pointers to touch information to allow loop on get xy_data */
static void cyttsp_init_tch_map(struct cyttsp *ts)
{
	ts->tch_map[0].tch = &ts->xy_data.regs.tch1;
	ts->tch_map[0].id = &ts->xy_data.regs.touch12_id;
	ts->tch_map[1].tch = &ts->xy_data.regs.tch2;
	ts->tch_map[1].id = &ts->xy_data.regs.touch12_id;
	ts->tch_map[2].tch = &ts->xy_data.regs.tch3;
	ts->tch_map[2].id = &ts->xy_data.regs.touch34_id;
	ts->tch_map[3].tch = &ts->xy_data.regs.tch4;
	ts->tch_map[3].id = &ts->xy_data.regs.touch34_id;
	ts->tch_map[4].tch = &ts->xy_data.xregs.tch5;
	ts->tch_map[4].id = &ts->xy_data.xregs.touch56_id;
	ts->tch_map[5].tch = &ts->xy_data.xregs.tch6;
	ts->tch_map[5].id = &ts->xy_data.xregs.touch56_id;
	ts->tch_map[6].tch = &ts->xy_data.xregs.tch7;
	ts->tch_map[6].id = &ts->xy_data.xregs.touch78_id;
	ts->tch_map[7].tch = &ts->xy_data.xregs.tch8;
	ts->tch_map[7].id = &ts->xy_data.xregs.touch78_id;
	ts->tch_map[8].tch = &ts->xy_data.xregs.tch9;
	ts->tch_map[8].id = &ts->xy_data.xregs.touch910_id;
	ts->tch_map[9].tch = &ts->xy_data.xregs.tch10;
	ts->tch_map[9].id = &ts->xy_data.xregs.touch910_id;
}

static int cyttsp_hndshk(struct cyttsp *ts, u8 hst_mode)
{
	int retval;
	u8 cmd;

	cmd = hst_mode & CY_HNDSHK_BIT ?
		hst_mode & ~CY_HNDSHK_BIT :
		hst_mode | CY_HNDSHK_BIT;

	retval = ttsp_write_block_data(ts, CY_REG_BASE,
		sizeof(cmd), (u8 *)&cmd, 0x67, true);

	return retval;
}

static void handle_multi_touch(struct cyttsp_trk *cur_trk, struct cyttsp *ts)
{
	u8 id;
	u8 cnt = 0;

	/* terminate any previous touch where the track
	 * is missing from the current event
	 */
	for (id = 0; id < CY_NUM_TRK_ID; id++) {
		if (cur_trk[id].tch) {
			/* put active current track data */
			input_report_abs(ts->input,
				ABS_MT_POSITION_X, cur_trk[id].x);
			input_report_abs(ts->input,
				ABS_MT_POSITION_Y, cur_trk[id].y);
			input_report_abs(ts->input,
				ABS_MT_TOUCH_MAJOR, cur_trk[id].z);
			input_mt_sync(ts->input);
			dev_dbg(ts->dev, "%s: MT% 2d: X=%d Y=%d Z=%d\n",
				__func__, id,
				cur_trk[id].x,
				cur_trk[id].y,
				cur_trk[id].z);
			/* save current touch xy_data as previous track data */
			ts->prv_trk[id] = cur_trk[id];
			cnt++;
		} else if (ts->prv_trk[id].tch) {
			/* put lift-off previous track data */
			input_report_abs(ts->input,
				ABS_MT_POSITION_X, ts->prv_trk[id].x);
			input_report_abs(ts->input,
				ABS_MT_POSITION_Y, ts->prv_trk[id].y);
			input_report_abs(ts->input,
				ABS_MT_TOUCH_MAJOR, CY_NTCH);
			input_mt_sync(ts->input);
			dev_dbg(ts->dev, "%s: MT% 2d: X=%d Y=%d Z=%d liftoff\n",
				__func__, id,
				ts->prv_trk[id].x,
				ts->prv_trk[id].y,
				CY_NTCH);
			/* clear previous touch indication */
			ts->prv_trk[id].tch = CY_NTCH;
			cnt++;
		}
	}

	/* signal the view motion event */
	if (cnt)
		input_sync(ts->input);
}

/* read xy_data for all current touches */
static int cyttsp_xy_worker(struct cyttsp *ts)
{
	u8 cur_tch = 0;
	u8 tch;
	u8 id;
	u8 *x;
	u8 *y;
	u8 z;
	struct cyttsp_trk cur_trk[CY_NUM_TRK_ID];

	/* Get event data from CYTTSP device.
	 * The event data includes all data
	 * for all active touches.
	 */
	if (ttsp_read_block_data(ts, CY_REG_BASE,
		sizeof(struct cyttsp_xydata_gen4), &ts->xy_data, 0x67, true))
		return 0;

	/* touch extension handling */
	if (ttsp_tch_ext(ts, &ts->xy_data))
		return 0;

	/* provide flow control handshake */
	if (ts->platform_data->use_hndshk)
		if (cyttsp_hndshk(ts, ts->xy_data.regs.hst_mode))
			return 0;

	/* determine number of currently active touches */
	cur_tch = GET_NUM_TOUCHES(ts->xy_data.regs.tt_stat);

	/* check for any error conditions */
	if (ts->power_state == CY_IDLE_STATE)
		return 0;
	else if (GET_BOOTLOADERMODE(ts->xy_data.regs.tt_mode)) {
		return -1;
	} else if (IS_LARGE_AREA(ts->xy_data.regs.tt_stat) == 1) {
		/* terminate all active tracks */
		cur_tch = CY_NTCH;
		dev_dbg(ts->dev, "%s: Large area detected\n", __func__);
	} else if (cur_tch > CY_NUM_TCH_ID) {
		/* terminate all active tracks */
		cur_tch = CY_NTCH;
		dev_dbg(ts->dev, "%s: Num touch error detected\n", __func__);
	} else if (IS_BAD_PKT(ts->xy_data.regs.tt_mode)) {
		/* terminate all active tracks */
		cur_tch = CY_NTCH;
		dev_dbg(ts->dev, "%s: Invalid buffer detected\n", __func__);
	}

	/* clear current touch tracking structures */
	memset(cur_trk, CY_NTCH, sizeof(cur_trk));

	/* extract xy_data for all currently reported touches */
	for (tch = 0; tch < cur_tch; tch++) {
		id = tch & 0x01 ?
			(*(ts->tch_map[tch].id) & 0x0F) :
			(*(ts->tch_map[tch].id) & 0xF0) >> 4;
		x = (u8 *)&((ts->tch_map[tch].tch)->x);
		y = (u8 *)&((ts->tch_map[tch].tch)->y);
		z = (ts->tch_map[tch].tch)->z;
		cur_trk[id].tch = CY_TCH;
		cur_trk[id].x = ((u16)x[0] << 8) + x[1];
		cur_trk[id].y = ts->platform_data->maxy - (((u16)y[0] << 8) + y[1]);
		cur_trk[id].z = z;
	}

	/* provide input event signaling for each active touch */
	handle_multi_touch(cur_trk, ts);

	return 0;
}

static void cyttsp_pr_state(struct cyttsp *ts)
{
	static char *cyttsp_powerstate_string[] = {
		"IDLE",
		"ACTIVE",
		"LOW_PWR",
		"SLEEP",
		"BOOTLOADER",
		"INVALID"
	};

	dev_info(ts->dev, "%s: %s\n", __func__,
		ts->power_state < CY_INVALID_STATE ?
		cyttsp_powerstate_string[ts->power_state] :
		"INVALID");
}

static irqreturn_t cyttsp_irq(int irq, void *handle)
{
	struct cyttsp *ts = handle;
	int retval;

	if (ts->power_state == CY_BL_STATE)
		complete(&ts->bl_ready);
	else {
		/* process the touches */
		retval = cyttsp_xy_worker(ts);

		if (retval < 0) {
			/* TTSP device has reset back to bootloader mode
			 * Reset driver touch history and restore
			 * operational mode
			 */
			memset(ts->prv_trk, CY_NTCH, sizeof(ts->prv_trk));
			retval = cyttsp_exit_bl_mode(ts);
			if (retval)
				ts->power_state = CY_IDLE_STATE;
			else
				ts->power_state = CY_ACTIVE_STATE;
			cyttsp_pr_state(ts);
		}
	}
	return IRQ_HANDLED;
}

static int cyttsp_power_on(struct cyttsp *ts)
{
	int retval = 0;

	if (!ts)
		return -ENOMEM;

	/* enable interrupts */
	retval = request_threaded_irq(ts->irq, NULL, cyttsp_irq,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		ts->platform_data->name, ts);

	if (retval < 0)
		ts->power_state = CY_IDLE_STATE;
	else
		ts->power_state = CY_ACTIVE_STATE;
	cyttsp_pr_state(ts);
	return retval;
}

#ifdef CONFIG_PM
int cyttsp_resume(void *handle)
{
	struct cyttsp *ts = handle;
	int retval = 0;
	struct cyttsp_xydata_gen4 xydata;

	/* From Firmware version 0X5B
	 * Reset pin should be pull low 20 ms here then pull high
	 * Now the action moved to earlysuspend.c
	 * It can be found by using CONFIG_INPUT_TOUCHSCREEN
	 */
	msleep(230);

	if (ts->platform_data->use_sleep && (ts->power_state !=
		CY_ACTIVE_STATE)) {
		if (ts->platform_data->wakeup) {
			retval = ts->platform_data->wakeup();
			if (retval < 0)
				dev_dbg(ts->dev, "%s: Error, wakeup failed!\n",
					__func__);
		} else {
			dev_dbg(ts->dev, "%s: Error, wakeup not implemented "
				"(check board file).\n", __func__);
			retval = -ENOSYS;
		}
		/* wake up device with a read */
		retval = ttsp_read_block_data(ts, CY_REG_BASE,
			sizeof(xydata), &xydata, 0x67, true);
		if (GET_HSTMODE(xydata.regs.hst_mode) == CY_OPERATE_MODE)
			ts->power_state = CY_ACTIVE_STATE;
	}
	dev_dbg(ts->dev, "%s: Wake Up %s\n", __func__,
		(retval < 0) ? "FAIL" : "PASS");
	return retval;
}
EXPORT_SYMBOL_GPL(cyttsp_resume);

int cyttsp_suspend(void *handle)
{
	struct cyttsp *ts = handle;
	u8 sleep_mode = 0;
	int retval = 0;

	if (ts->platform_data->use_sleep &&
		(ts->power_state == CY_ACTIVE_STATE)) {
		sleep_mode = CY_DEEP_SLEEP_MODE;
		retval = ttsp_write_block_data(ts, CY_REG_BASE,
			sizeof(sleep_mode), &sleep_mode, 0x67, true);
		if (!(retval < 0))
			ts->power_state = CY_SLEEP_STATE;
	}
	dev_dbg(ts->dev, "%s: Sleep Power state is %s\n", __func__,
		(ts->power_state == CY_ACTIVE_STATE) ?
		"ACTIVE" :
		((ts->power_state == CY_SLEEP_STATE) ?
		"SLEEP" : "LOW POWER"));
	return retval;
}
EXPORT_SYMBOL_GPL(cyttsp_suspend);

static void cyttsp_ts_early_suspend(struct early_suspend *h)
{
	struct cyttsp *ts = container_of(h, struct cyttsp, early_suspend);

	printk("%s enter\n", __func__);

	disable_irq(ts->irq);

	if (!ts->suspended) {
		ts->suspended = 1;
		cyttsp_suspend(ts);
	}

	printk("%s leave\n", __func__);
}

static void cyttsp_ts_late_resume(struct early_suspend *h)
{
	struct cyttsp *ts = container_of(h, struct cyttsp, early_suspend);

	printk("%s enter\n", __func__);
	mdelay(100);
	if (ts->suspended) {
		ts->suspended = 0;
		if (cyttsp_resume(ts) < 0)
			printk(KERN_ERR "%s: Error, cyttsp_resume.\n", __func__);
	}

	enable_irq(ts->irq);

	printk("%s leave\n", __func__);
}
#endif

static int cyttsp_open(struct input_dev *dev)
{
	int retval = 0;
	struct cyttsp *ts = input_get_drvdata(dev);

	if (!ts->powered) {
		retval = cyttsp_power_on(ts);

		/* Powered if no hard failure */
		if (retval == 0)
			ts->powered = true;
		else
			ts->powered = false;
	}

	return retval;
}

void cyttsp_core_release(void *handle)
{
	struct cyttsp *ts = handle;

	if (ts) {
		mutex_destroy(&ts->mutex);
		free_irq(ts->irq, ts);
		input_unregister_device(ts->input);
		if (ts->platform_data->exit)
			ts->platform_data->exit();
		kfree(ts);
	}
	gpio_free(TEGRA_GPIO_PQ7);
}
EXPORT_SYMBOL_GPL(cyttsp_core_release);

static void cyttsp_close(struct input_dev *dev)
{
	struct cyttsp *ts = input_get_drvdata(dev);

	if (ts->powered)
		free_irq(ts->irq, ts);

	ts->powered = false;
}

static ssize_t version_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	int retval, tries;
	u8 cmd = CY_SYSINFO_MODE;

	memset(&(g_ts->sysinfo_data), 0, sizeof(struct cyttsp_sysinfo_data));
	/* switch to sysinfo mode */
	retval = ttsp_write_block_data(g_ts, CY_REG_BASE, sizeof(cmd), &cmd, 0x67, true);
	if (retval < 0)
		return sprintf(buf, "cypress touch switch to sysinfo mode failed!!\n");

	/* read sysinfo registers */
	tries = 0;
	do {
		msleep(CY_DELAY_DFLT);
		retval = ttsp_read_block_data(g_ts, CY_REG_BASE, sizeof(g_ts->sysinfo_data), &(g_ts->sysinfo_data), 0x67, true);
	} while (!((retval == 0) &&
		!((g_ts->sysinfo_data.tts_verh == 0) &&
		(g_ts->sysinfo_data.tts_verl == 0))) &&
		(tries++ < CY_DELAY_MAX));
	cyttsp_set_operational_mode(g_ts);

	return sprintf(buf, "cypress FW version:0x%02X%02X\n", g_ts->sysinfo_data.app_verh, g_ts->sysinfo_data.app_verl);

}

static DEVICE_ATTR(FirmwareVersion, 0644, version_show, NULL);

static struct kobject *TOUCH;
static int TOUCH_attr_init(void)
{
	int ret;

	printk(KERN_INFO "TOUCH:kobject creat and add\n");
	TOUCH = NULL;
	TOUCH = kobject_create_and_add("TOUCH", NULL);
	if (TOUCH == NULL) {
		printk(KERN_INFO "TOUCH: subsystem_register failed\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "TOUCH:sysfs_create_file\n");
	ret = sysfs_create_file(TOUCH, &dev_attr_FirmwareVersion.attr);

	if (ret) {
		printk(KERN_INFO "TOUCH: sysfs_create_file failed\n");
		kobject_del(TOUCH);
	}
	return 0 ;
}

#ifdef CONFIG_TOUCHSCREEN_CYLSTS_FBL
#include "cylsts_fbl.h"
#endif

void *cyttsp_core_init(struct cyttsp_bus_ops *bus_ops, struct device *dev)
{
	struct input_dev *input_device;
	struct cyttsp *ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	int ret = 0;

	if (!ts) {
		dev_dbg(ts->dev, "%s: Error, kzalloc\n", __func__);
		goto error_alloc_data;
	}

	if ((dev == NULL) || (bus_ops == NULL)) {
		kfree(ts);
		goto error_alloc_data;
	}

	mutex_init(&ts->mutex);
	ts->dev = dev;
	ts->platform_data = dev->platform_data;
	ts->bus_ops = bus_ops;
	init_completion(&ts->bl_ready);

	if (ts->platform_data->init) {
		if (ts->platform_data->init()) {
			dev_dbg(ts->dev, "%s: Error, platform init failed!\n",
				__func__);
			goto error_init;
		}
	}

	ts->irq = gpio_to_irq(ts->platform_data->irq_gpio);
	if (ts->irq <= 0) {
		dev_dbg(ts->dev, "%s: Error, failed to allocate irq\n",
			__func__);
			goto error_init;
	}

#ifdef CONFIG_TOUCHSCREEN_CYLSTS_FBL
	dev_dbg(ts->dev, "%s: start fbl init\n", __func__);
pr_info("%s: start fbl init\n", __func__);
	ts->fw_fbl = cyttsp_fbl_init(ts);
	if (!ts->fw_fbl) {
		dev_dbg(ts->dev, "%s: Error, failed to allocate fbl\n",
			__func__);
		goto error_init;
	}
#endif

	/* Create the input device and register it. */
	input_device = input_allocate_device();
	if (!input_device) {
		dev_dbg(ts->dev, "%s: Error, failed to allocate input device\n",
			__func__);
		goto error_input_allocate_device;
	}

	ts->input = input_device;

	//input_device->name = ts->platform_data->name;
	input_device->name = "atmel-maxtouch"; // for ACER Honeycomb...

	snprintf(ts->phys, sizeof(ts->phys), "%s", dev_name(dev));
	input_device->phys = ts->phys;
	input_device->dev.parent = ts->dev;
	ts->bus_type = bus_ops->dev->bus;
	ts->powered = false;
	input_device->open = cyttsp_open;
	input_device->close = cyttsp_close;
	input_set_drvdata(input_device, ts);

	cyttsp_init_tch_map(ts);
	memset(ts->prv_trk, CY_NTCH, sizeof(ts->prv_trk));

	__set_bit(EV_SYN, input_device->evbit);
	__set_bit(EV_KEY, input_device->evbit);
	__set_bit(EV_ABS, input_device->evbit);

	input_set_abs_params(input_device, ABS_MT_POSITION_X,
		0, ts->platform_data->maxx, 0, 0);
	input_set_abs_params(input_device, ABS_MT_POSITION_Y,
		0, ts->platform_data->maxy, 0, 0);
	input_set_abs_params(input_device, ABS_MT_TOUCH_MAJOR,
		0, CY_MAXZ, 0, 0);

	TOUCH_attr_init();
	g_ts = ts;

	tegra_gpio_enable(TEGRA_GPIO_PQ7);
	ret = gpio_request(TEGRA_GPIO_PQ7, "CYTTSP_RESET");
	if (ret)
		pr_err("%s: Failed to request GPIO %d\n", __func__, TEGRA_GPIO_PQ7);

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 5;
	ts->early_suspend.suspend = cyttsp_ts_early_suspend;
	ts->early_suspend.resume = cyttsp_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	if (input_register_device(input_device)) {
		dev_dbg(ts->dev, "%s: Error, failed to register input device\n",
			__func__);
		goto error_input_register_device;
	}

	goto no_error;

error_input_register_device:
	input_unregister_device(input_device);
error_input_allocate_device:
	if (ts->platform_data->exit)
		ts->platform_data->exit();
error_init:
	mutex_destroy(&ts->mutex);
	kfree(ts);
pr_err("%s: core_init fail\n", __func__);
error_alloc_data:
no_error:
	return ts;
}
EXPORT_SYMBOL_GPL(cyttsp_core_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard touchscreen driver core");
MODULE_AUTHOR("Cypress");
