/*
 * kernel/drivers/media/video/tegra
 *
 * Aptina MT9P111 sensor driver
 *
 * Copyright (C) 2010 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <media/yuv5_sensor.h>

#if defined(CONFIG_LSC_FROM_EC)
#include <media/lsc_from_ec.h>
#endif

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
#include "yuv5_init_tab_picasso.h"
#endif
#if defined(CONFIG_MACH_ACER_VANGOGH)
#include "yuv5_init_tab_vangogh.h"
#endif

#define CAM_CORE_A_OUTPUT_SIZE_WIDTH 0xC86C
#define CAM_CORE_B_OUTPUT_SIZE_WIDTH 0xC8C0
#define SENSOR_5M_640_WIDTH_VAL 0x0518
#define CAM_CORE_A_OUTPUT_SIZE_WIDTH 0xC86C
#define ONE_TIME_TABLE_CHECK_REG 0x8016
#define ONE_TIME_TABLE_CHECK_DATA 0x086C

#define MAX_RETRIES 3
extern int tegra_camera_enable_csi_power(void);
extern int tegra_camera_disable_csi_power(void);

static int preview_mode = SENSOR_MODE_1280x960;
static u16 lowlight_reg = 0xB804;
static u16 lowlight_threshold = 0x3000;
static u16 lowlight_fps_threshold = 19;
static u16 sharpness_reg = 0xB81A;
// TODO: sharpness_threshold should be fine tuned
static u16 sharpness_threshold = 0x009F;

static int sensor_read_reg(struct i2c_client *client, u16 addr, u16 *val)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[4];

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = data;

	/* high byte goes out first */
	data[0] = (u8) (addr >> 8);
	data[1] = (u8) (addr & 0xff);

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = data + 2;

	err = i2c_transfer(client->adapter, msg, 2);
	if (err != 2)
		return -EINVAL;

	swap(*(data+2), *(data+3)); //swap high and low byte to match table format
	memcpy(val, data+2, 2);

	return 0;
}

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
static int sensor_read_reg8(struct i2c_client *client, u16 addr, u8 *val)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[3];

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = data;

	/* high byte goes out first */
	data[0] = (u8) (addr >> 8);;
	data[1] = (u8) (addr & 0xff);

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = data + 2;

	err = i2c_transfer(client->adapter, msg, 2);
	if (err != 2)
		return -EINVAL;

	memcpy(val, data+2, 1);

	return 0;
}
#endif

static int sensor_write_reg8(struct i2c_client *client, u16 addr, u16 val)
{
	int err;
	struct i2c_msg msg;
	unsigned char data[3];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) (addr >> 8);
	data[1] = (u8) (addr & 0xff);
	data[2] = (u8) (val & 0xff);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = data;

	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			return 0;
		retry++;
		pr_err("yuv5 %s: i2c transfer failed, retrying %x %x\n",
			__func__, addr, val);
		msleep(3);
	} while (retry <= SENSOR_5M_MAX_RETRIES);

	return err;
}

#ifdef CONFIG_VIDEO_LTC3216
extern void ltc3216_turn_on_flash(void);
extern void ltc3216_turn_off_flash(void);
extern void ltc3216_turn_on_torch(void);
extern void ltc3216_turn_off_torch(void);

static struct workqueue_struct *torch_off_wq;
static int torch_counter = 0;

static void torch_off_work_func(struct work_struct *work)
{
	sensor_read_reg(info->i2c_client, sharpness_reg, &info->sharpness);
	pr_info("yuv5 %s(): AF is timeout, sharpness = 0x%04x\n",
		__func__, info->sharpness);
	info->af_lowlight_lock = 0;
	ltc3216_turn_off_torch();
}

static void torch_off_timer_func(unsigned long _data)
{
	pr_debug("yuv5 %s\n", __func__);

	if (torch_counter > 0) {
		if (torch_counter == 1)
			queue_work(torch_off_wq, &info->work);
		else {
			torch_counter--;
			mod_timer(&info->torch_off_timer, jiffies + msecs_to_jiffies(100));
		}
	}
}
#endif

static int sensor_poll(struct i2c_client *client, u16 addr, u16 value)
{
	u16 data;
	int try, err;

	for (try = 0; try < SENSOR_5M_POLL_RETRIES; try++) {
		err = sensor_read_reg(client, addr, &data);
		if (err)
			return err;
		pr_debug("yuv5 %s: poll addr reg 0x%04x, value=0x%04x, data=0x%04x, try=%d times\n",
			__func__, addr, value, data, try);
		if (value == data)
			return 0;
	}
	pr_err("yuv5 %s: poll for %d times %x == ([%x]=%x) failed\n",
		__func__, try, value, addr, data);

	return -EIO;
}

static int sensor_poll_bit_set(struct i2c_client *client, u16 addr, u16 bit)
{
	return sensor_poll(client, addr, bit);
}

static int sensor_write_reg16(struct i2c_client *client, u16 addr, u16 val)
{
	int err;
	struct i2c_msg msg;
	unsigned char data[4];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) (addr >> 8);
	data[1] = (u8) (addr & 0xff);
	data[2] = (u8) (val >> 8);
	data[3] = (u8) (val & 0xff);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 4;
	msg.buf = data;

	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			return 0;
		retry++;
		pr_err("yuv5 %s: i2c transfer failed, retrying %x %x\n",
			__func__, addr, val);
		msleep(3);
	} while (retry <= SENSOR_5M_MAX_RETRIES);

	return err;
}

static int sensor_get_fps(void)
{
	u16 frame_length = 0;
	u32 frame_rate = 0;

	sensor_read_reg(info->i2c_client, 0x300A, &frame_length);
	if (frame_length != 0) {
		// long frame_rate = 118154000 / (3846 * frame_length);
		frame_rate = 30721 / frame_length;
		pr_info("yuv5 %s: frame_length = %d, frame_rate = %d\n",
			__func__, frame_length, frame_rate);
	}

	return frame_rate;
}

static int sensor_write_table(struct i2c_client *client, const struct sensor_reg table[])
{
	const struct sensor_reg *next;
	int err;

	next = table ;

	pr_debug("yuv5 %s\n", __func__);
	for (next = table; next->op != SENSOR_5M_TABLE_END; next++) {
		switch (next->op) {
			case WRITE_REG_DATA8:
				err = sensor_write_reg8(client, next->addr, next->val);
				if (err)
					return err;
				break;

			case WRITE_REG_DATA16:
				err = sensor_write_reg16(client, next->addr, next->val);
				if (err)
					return err;
				break;

			case SENSOR_5M_WAIT_MS:
				msleep(next->val);
				break;

			case POLL_REG_BIT:
				err = sensor_poll_bit_set(client, next->addr, next->val);
				if (err)
					return err;
				break;

			case SW_FLASH_CHECK_POINT:
#ifdef CONFIG_VIDEO_LTC3216
				if (info->focus_mode == SENSOR_AF_FULLTRIGER) {
					pr_info("yuv5 %s: SW_FLASH_CHECK_POINT info->flash_mode=%d, af_lowlight=%d\n",
						__func__, info->flash_mode, info->af_lowlight);
					if (info->flash_mode == YUV_5M_FlashControlOn ||
						(info->flash_mode == YUV_5M_FlashControlAuto && info->af_lowlight)) {
						ltc3216_turn_on_flash();
						info->flash_status = 1;
					}
				} else if (info->focus_mode == SENSOR_AF_INFINITY) {
					u16 val;
					sensor_read_reg(info->i2c_client, lowlight_reg, &val);
					if (info->flash_mode == YUV_5M_FlashControlOn ||
						(info->flash_mode == YUV_5M_FlashControlAuto &&
						(val < lowlight_threshold || sensor_get_fps() < lowlight_fps_threshold))) {
						ltc3216_turn_on_flash();
						info->flash_status = 1;
					}
				}
#endif
				break;

			default:
				pr_err("yuv5 %s: invalid operation 0x%x\n", __func__, next->op);
				return err;
		}
	}

	return 0;
}

static int get_sensor_current_width(struct i2c_client *client, u16 *val, u16 reg)
{
	int err;

	err = sensor_read_reg(client, reg, val);
	if (err)
		return err;

	return 0;
}

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
static int check_otpm_mode(struct i2c_client *client)
{
	int err;
	u8 val;
#if defined(CONFIG_LSC_FROM_EC)
	int retry = 0;
#endif

	pr_debug("yuv5 %s\n", __func__);

	// io_nv_mem_command: io_nvmem_probe
	// look for OTPM and check if it is Micron Format
	err = sensor_write_reg16(info->i2c_client, 0x098E, 0xE02A);
	if (err) {
		info->otpm_mode = -1;
		return -1;
	}
	err = sensor_write_reg16(info->i2c_client, 0xE02A, 0x0001);
	if (err) {
		info->otpm_mode = -2;
		return -2;
	}

	msleep(100);

	// io_nv_mem_status
	// check if OTPM is formatted correctly
	err = sensor_read_reg8(info->i2c_client, 0xE023, &val);
	if (err) {
		info->otpm_mode = -3;
		return -3;
	}
	pr_info("yuv5 %s: io_nv_mem_status = %x\n", __func__, val);

	// OTPM is not formatted correctly, patch ram is used
	if (val == 0x41) {
		// patch ram from default data
		info->otpm_mode = 0;
#if defined(CONFIG_LSC_FROM_EC)
		do {
			// lsc_from_ec_status == 0 means EC data reading is not done
			// EC data reading takes about 10 seconds
			if (lsc_from_ec_status != 0)
				break;
			retry++;
			pr_info("yuv5 %s: lsc_from_ec not ready, delay 1 sec\n", __func__);
			msleep(1000);
		} while (retry < SENSOR_5M_MAX_RETRIES);

		// patch ram from EC
		if (lsc_from_ec_status == 1)
			info->otpm_mode = 2;
#endif
	}
	// OTPM is formatted correctly
	else if (val == 0xC1)
		info->otpm_mode = 1;
	else
		info->otpm_mode = -4;

	return info->otpm_mode;
}
#endif

static int sensor_set_mode(struct sensor_info *info, struct sensor_5m_mode *mode)
{
	int sensor_table;
	int err;
	u16 val;

	pr_info("yuv5 %s: xres %u yres %u\n", __func__, mode->xres, mode->yres);

	if (mode->xres == 2592 && mode->yres == 1944)
		sensor_table = SENSOR_MODE_2592x1944;
	else if (mode->xres == 1280 && mode->yres == 960)
		sensor_table = SENSOR_MODE_1280x960;
	else if (mode->xres == 1280 && mode->yres == 720)
		sensor_table = SENSOR_MODE_1280x720;
	else if (mode->xres == 640 && mode->yres == 480)
		sensor_table = SENSOR_MODE_640x480;
	else {
		pr_err("yuv5 %s: invalid resolution supplied to set mode %d %d\n",
			__func__, mode->xres, mode->yres);
		return -EINVAL;
	}

	err = get_sensor_current_width(info->i2c_client, &val, CAM_CORE_A_OUTPUT_SIZE_WIDTH);
	pr_info("yuv5 %s: get sensor current context A width %x\n", __func__, val);

#ifdef CONFIG_VIDEO_LTC3216
	// switch back to preview mode and suppose capture is done, so turn off the flash LED
	if (sensor_table == preview_mode) {
		ltc3216_turn_off_flash();
		info->flash_status = 0;
	}
#endif

	//check already program the sensor mode, Aptina support Context B fast switching capture mode back to preview mode
	//we don't need to re-program the sensor mode for 640x480 table
	if ((val != SENSOR_5M_640_WIDTH_VAL) && (sensor_table == SENSOR_MODE_2592x1944)) {
		pr_info("yuv5 %s: initialize cts table %d\n", __func__, sensor_table);
		err = sensor_write_table(info->i2c_client, mode_2592x1944_CTS);
		if (err)
			return err;

	}

	if (sensor_table == SENSOR_MODE_2592x1944) {
		pr_info("yuv5 %s: sensor_table == SENSOR_MODE_2592x1944\n", __func__);
		err = sensor_write_table(info->i2c_client, is_preview);
		if (err)
			return err;

		info->capture_frame_rate = sensor_get_fps();

		err = sensor_write_table(info->i2c_client, mode_table[sensor_table]);
		if (err)
			return err;
		err = sensor_write_table(info->i2c_client, is_capture);
		if (err)
			return err;
	}

	if (!((val == SENSOR_5M_640_WIDTH_VAL) && (sensor_table == preview_mode))) {
		pr_info("yuv5 %s: (!((val == SENSOR_5M_640_WIDTH_VAL) && (sensor_table == preview_mode)))\n", __func__);
		// the sensor table has been updated, needn't do it again
		if (sensor_table == SENSOR_MODE_2592x1944)
			return 0 ;

		pr_info("yuv5 %s: initialize sensor table %d\n", __func__, sensor_table);
		err = sensor_write_table(info->i2c_client, mode_table[sensor_table]);
		if (err)
			return err;
		err = sensor_write_table(info->i2c_client, is_preview);
		if (err)
			return err;
	}

	if ((val == SENSOR_5M_640_WIDTH_VAL) && (sensor_table == preview_mode)) {
		pr_info("yuv5 %s: ((val == SENSOR_5M_640_WIDTH_VAL) && (sensor_table == preview_mode))\n", __func__);
		err = sensor_write_table(info->i2c_client, is_preview);
		if (err)
			return err;
	}

	info->mode = sensor_table;
	// One time programming table start ONE_TIME_TABLE_CHECK_REG
	err = get_sensor_current_width(info->i2c_client, &val, ONE_TIME_TABLE_CHECK_REG);
	if (!(val == ONE_TIME_TABLE_CHECK_DATA)) {
		int otpm_mode = 0;

		pr_info("yuv5 %s: first time initialize one time table\n", __func__);
		err = sensor_write_table(info->i2c_client, af_load_fw);
		if (err)
			return err;

		err = sensor_write_table(info->i2c_client, char_settings);
		if (err)
			return err;

		err = sensor_write_table(info->i2c_client, awb_setting);
		if (err)
			return err;

		err = sensor_write_table(info->i2c_client, pa_calib);
		if (err)
			return err;

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
		otpm_mode = check_otpm_mode(info->i2c_client);
#endif
#if defined(CONFIG_MACH_ACER_VANGOGH)
		otpm_mode = 1;
#endif

		if (otpm_mode == 0) {
			pr_info("yuv5 %s: value == 0x41, it means OTPM empty\n", __func__);
			sensor_write_table(info->i2c_client, enable_PGA_patch_mem_table_start);
			sensor_write_table(info->i2c_client, default_A_patch_ram_table);
			sensor_write_table(info->i2c_client, default_CWF_patch_ram_table);
			sensor_write_table(info->i2c_client, default_D65_patch_ram_table);
			sensor_write_table(info->i2c_client, enable_PGA_patch_mem_table_end);
		}
		else if (otpm_mode == 1) {
			pr_info("yuv5 %s: value == 0xC1, it means OTPM has data inside\n", __func__);
			sensor_write_table(info->i2c_client, enable_PGA_OTPM_table);
		}
#if defined(CONFIG_LSC_FROM_EC)
		else if (otpm_mode == 2) {
			pr_info("yuv5 %s: value == 0x41, but EC has LSC data\n", __func__);
			sensor_write_table(info->i2c_client, enable_PGA_patch_mem_table_start);
			sensor_write_table(info->i2c_client, default_A_patch_ram_table);
			sensor_write_table(info->i2c_client, (struct sensor_reg *)EC_CWF_patch_ram_table);
			sensor_write_table(info->i2c_client, (struct sensor_reg *)EC_D65_patch_ram_table);
			sensor_write_table(info->i2c_client, enable_PGA_patch_mem_table_end);
		}
#endif
		else
			pr_info("yuv5 %s: check_otpm_mode failed %d\n", __func__, otpm_mode);
	}

	return 0;
}

static int sensor_set_af_mode(struct sensor_info *info, u8 mode)
{
	int err;
	u16 val;

	pr_info("yuv5 %s: mode %d\n", __func__, mode);

	if (info->af_lowlight_lock == 0) {
		sensor_read_reg(info->i2c_client, lowlight_reg, &val);
		pr_info("yuv5 %s: reg 0x%04X=0x%04X\n", __func__, lowlight_reg, val);
		pr_info("yuv5 %s: info->flash_mode=%d\n", __func__, info->flash_mode);
		if (mode != SENSOR_AF_INFINITY &&
			(info->flash_mode == YUV_5M_FlashControlOn ||
				(info->flash_mode == YUV_5M_FlashControlAuto &&
				(val < lowlight_threshold || sensor_get_fps() < lowlight_fps_threshold)))) {
			info->af_lowlight_lock = 1;
			info->af_lowlight = 1;
			info->af_lowlight_val = val;
			info->sharpness = 0x0000;
#ifdef CONFIG_VIDEO_LTC3216
			ltc3216_turn_on_torch();
			// this value sync with nvomxcamera.cpp focus timeout value = 20 * 100ms = 2000ms
			torch_counter = 20;
			mod_timer(&info->torch_off_timer, jiffies + msecs_to_jiffies(100));
#endif
		} else {
			pr_info("yuv5 %s: reset af_lowlight and af_lowlight_val to 0\n", __func__);
			info->af_lowlight = 0;
			info->af_lowlight_val = 0;
		}
	}
	info->focus_mode = mode;

	err = sensor_write_table(info->i2c_client, af_mode_table[mode]);
	if (err)
		return err;

	return 0;
}

static int sensor_get_af_status(struct sensor_info *info)
{
	int err, ret;
	u16 val;

	pr_debug("yuv5 %s\n", __func__);
	err = sensor_write_reg16(info->i2c_client, 0x098E, 0xB000);
	if (err)
		return err;
	err = sensor_read_reg(info->i2c_client, 0xB000, &val);
	if (err)
		return err;
	pr_info("yuv5 %s: value %x\n", __func__, val);

	// af_status_finished_focusing_lens
	if (val & 0x0010) {
		// AF finished
#ifdef CONFIG_VIDEO_LTC3216
		del_timer(&(info->torch_off_timer));
		torch_counter = 0;
		info->af_lowlight_lock = 0;
		ltc3216_turn_off_torch();
#endif
		sensor_read_reg(info->i2c_client, sharpness_reg, &info->sharpness);
		pr_info("yuv5 %s: AF finished, sharpness = 0x%04x\n",
			__func__, info->sharpness);
		if (info->sharpness > sharpness_threshold)
			// NvCameraIspFocusStatus_Locked
			ret = 1;
		else
			// NvCameraIspFocusStatus_FailedToFind
			ret = 2;
	} else {
		// NvCameraIspFocusStatus_Busy
		ret = 0;
	}

	return ret;
}

static int sensor_get_flash_status(struct sensor_info *info)
{
	return info->flash_status;
}

static long sensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sensor_info *info = file->private_data;
	int err=0;

	pr_debug("yuv5 %s: cmd %u\n", __func__, cmd);
	switch (cmd) {
		case SENSOR_5M_IOCTL_SET_MODE: {
			struct sensor_5m_mode mode;

			if (copy_from_user(&mode,
					(const void __user *)arg,
					sizeof(struct sensor_5m_mode))) {
				return -EFAULT;
			}
			pr_debug("yuv5 %s: set_mode\n", __func__);

			return sensor_set_mode(info, &mode);
		}

		case SENSOR_5M_IOCTL_GET_STATUS:
			pr_info("yuv5 %s: get_status\n", __func__);
			return 0;

		case SENSOR_5M_IOCTL_GET_AF_STATUS:
			pr_debug("yuv5 %s: get_af_status\n", __func__);
			return sensor_get_af_status(info);

		case SENSOR_5M_IOCTL_SET_AF_MODE:
			pr_debug("yuv5 %s: set_af_mode\n", __func__);
			return sensor_set_af_mode(info, arg);

		case SENSOR_5M_IOCTL_GET_FLASH_STATUS:
			pr_debug("yuv5 %s: get_flash_status\n", __func__);
			return sensor_get_flash_status(info);

		case SENSOR_5M_IOCTL_SET_COLOR_EFFECT: {
			u8 coloreffect;

			if (copy_from_user(&coloreffect,
					(const void __user *)arg,
					sizeof(coloreffect))) {
				return -EFAULT;
			}
			pr_info("yuv5 %s: coloreffect %d\n", __func__, coloreffect);

			switch(coloreffect) {
				case YUV_5M_ColorEffect_None:
					err = sensor_write_table(info->i2c_client, ColorEffect_None);
					break;

				case YUV_5M_ColorEffect_Mono:
					err = sensor_write_table(info->i2c_client, ColorEffect_Mono);
					break;

				case YUV_5M_ColorEffect_Sepia:
					err = sensor_write_table(info->i2c_client, ColorEffect_Sepia);
					break;

				case YUV_5M_ColorEffect_Negative:
					err = sensor_write_table(info->i2c_client, ColorEffect_Negative);
					break;

				case YUV_5M_ColorEffect_Solarize:
					err = sensor_write_table(info->i2c_client, ColorEffect_Solarize);
					break;

				case YUV_5M_ColorEffect_Posterize:
					err = sensor_write_table(info->i2c_client, ColorEffect_Posterize);
					break;

				default:
					break;
			}
			if (err)
				return err;
			return 0;
		}

		case SENSOR_5M_IOCTL_SET_WHITE_BALANCE: {
			u8 whitebalance;

			if (copy_from_user(&whitebalance,
					(const void __user *)arg,
					sizeof(whitebalance))) {
				return -EFAULT;
			}
			pr_info("yuv5 %s: whitebalance %d\n", __func__, whitebalance);

			switch(whitebalance) {
				case YUV_5M_Whitebalance_Auto:
					err = sensor_write_table(info->i2c_client, Whitebalance_Auto);
					break;

				case YUV_5M_Whitebalance_Incandescent:
					err = sensor_write_table(info->i2c_client, Whitebalance_Incandescent);
					break;

				case YUV_5M_Whitebalance_Daylight:
					err = sensor_write_table(info->i2c_client, Whitebalance_Daylight);
					break;

				case YUV_5M_Whitebalance_Fluorescent:
					err = sensor_write_table(info->i2c_client, Whitebalance_Fluorescent);
					break;

				case YUV_5M_Whitebalance_CloudyDaylight:
					err = sensor_write_table(info->i2c_client, Whitebalance_CloudyDaylight);
					break;

				default:
					break;
			}
			if (err)
				return err;
			return 0;
		}

		// 5140 didn't support scene mode
		case SENSOR_5M_IOCTL_SET_SCENE_MODE: {
			u8 scenemode;
			if (copy_from_user(&scenemode,
					(const void __user *)arg,
					sizeof(scenemode))) {
				return -EFAULT;
			}
			pr_info("yuv5 %s: scenemode %d\n", __func__, scenemode);

			switch(scenemode) {
				case YUV_5M_SceneMode_Auto:
					err = sensor_write_table(info->i2c_client, scene_auto);
					break;

				case YUV_5M_SceneMode_Action:
					err = sensor_write_table(info->i2c_client, scene_action);
					break;

				case YUV_5M_SceneMode_Portrait:
					err = sensor_write_table(info->i2c_client, scene_portrait);
					break;

				case YUV_5M_SceneMode_Landscape:
					err = sensor_write_table(info->i2c_client, scene_landscape);
					break;

				case YUV_5M_SceneMode_Night:
					err = sensor_write_table(info->i2c_client, scene_night);
					break;

				case YUV_5M_SceneMode_NightPortrait:
					err = sensor_write_table(info->i2c_client, scene_nightportrait);
					break;

				case YUV_5M_SceneMode_Theatre:
					err = sensor_write_table(info->i2c_client, scene_theatre);
					break;

				case YUV_5M_SceneMode_Beach:
					err = sensor_write_table(info->i2c_client, scene_beach);
					break;

				case YUV_5M_SceneMode_Snow:
					err = sensor_write_table(info->i2c_client, scene_snow);
					break;

				case YUV_5M_SceneMode_Sunset:
					err = sensor_write_table(info->i2c_client, scene_sunset);
					break;

				case YUV_5M_SceneMode_SteadyPhoto:
					err = sensor_write_table(info->i2c_client, scene_steadyphoto);
					break;

				case YUV_5M_SceneMode_Fireworks:
					err = sensor_write_table(info->i2c_client, scene_fireworks);
					break;

				default:
					break;
			}
			return 0;
		}

		case SENSOR_5M_IOCTL_SET_EXPOSURE: {
			u8 exposure;

			if (copy_from_user(&exposure,
					(const void __user *)arg,
					sizeof(exposure))) {
				return -EFAULT;
			}
			pr_info("yuv5 %s: exposure %d\n", __func__, exposure);

			switch(exposure) {
				case YUV_5M_Exposure_0:
					err = sensor_write_table(info->i2c_client, exp_zero);
					break;

				case YUV_5M_Exposure_1:
					err = sensor_write_table(info->i2c_client, exp_one);
					break;

				case YUV_5M_Exposure_2:
					err = sensor_write_table(info->i2c_client, exp_two);
					break;

				case YUV_5M_Exposure_Negative_1:
					err = sensor_write_table(info->i2c_client, exp_negative1);
					break;

				case YUV_5M_Exposure_Negative_2:
					err = sensor_write_table(info->i2c_client, exp_negative2);
					break;

				default:
					break;
			}
			return 0;
		}

#ifdef CONFIG_VIDEO_LTC3216
		case SENSOR_5M_IOCTL_SET_FLASH_MODE: {
			u8 flashmode;

			if (copy_from_user(&flashmode,
					(const void __user *)arg,
					sizeof(flashmode))) {
				return -EFAULT;
			}
			pr_info("yuv5 %s: flashmode %d\n", __func__, (int)flashmode);
			info->flash_mode = (int) flashmode;

			switch (flashmode) {
				case YUV_5M_FlashControlOn:
					break;

				case YUV_5M_FlashControlOff:
					ltc3216_turn_off_flash();
					break;

				case YUV_5M_FlashControlAuto:
					break;

				case YUV_5M_FlashControlTorch:
					ltc3216_turn_on_torch();
					break;

				default:
					break;
			}
			return 0;
		}

#endif
		case SENSOR_5M_IOCTL_GET_CAPTURE_FRAME_RATE:
			if (copy_to_user((void __user *)arg,
					&(info->capture_frame_rate),
					sizeof(info->capture_frame_rate))) {
				return -EFAULT;
			}
			pr_info("yuv5 %s: get_capture_frame_rate\n", __func__);
			return 0;

		case SENSOR_5M_IOCTL_CAPTURE_CMD:
			pr_info("yuv5 %s: capture cmd\n", __func__);
			sensor_write_table(info->i2c_client, mode_vga_capture_cmd);
			return 0;

		default:
			pr_info("yuv5 %s: default\n", __func__);
			return -EINVAL;
	}

	return 0;
}

static int sensor_open(struct inode *inode, struct file *file)
{
	pr_info("yuv5 %s\n", __func__);
	file->private_data = info;
	if (info->pdata && info->pdata->power_on)
		info->pdata->power_on();

	return 0;
}

int sensor_5m_release(struct inode *inode, struct file *file)
{
	pr_info("yuv5 %s\n", __func__);
#ifdef CONFIG_VIDEO_LTC3216
	ltc3216_turn_off_flash();
#endif
	if (info->pdata && info->pdata->power_off)
		info->pdata->power_off();
	file->private_data = NULL;

	return 0;
}

static const struct file_operations sensor_fileops = {
	.owner = THIS_MODULE,
	.open = sensor_open,
	.unlocked_ioctl = sensor_ioctl,
	.release = sensor_5m_release,
};

static struct miscdevice sensor_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = SENSOR_5M_NAME,
	.fops = &sensor_fileops,
};

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	int device_check_ret, retry = 0;
	u16 read_val = 0;

	pr_info("yuv5 %s\n", __func__);

	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);

	if (!info) {
		pr_err("yuv5 %s: Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}

	info->pdata = client->dev.platform_data;
	info->i2c_client = client;

	tegra_camera_enable_csi_power();
	if (info->pdata && info->pdata->power_on)
		info->pdata->power_on();

	do {
		device_check_ret = sensor_read_reg(client, 0x0000, &read_val);
		if (device_check_ret == 0)
			break;
		retry++;
		msleep(3);
	} while (retry < MAX_RETRIES);

	if (info->pdata && info->pdata->power_off)
		info->pdata->power_off();
	tegra_camera_disable_csi_power();

	if (device_check_ret != 0) {
		pr_err("yuv5 %s: cannot read chip_id\n", __func__);
		kfree(info);
		return device_check_ret;
	}

	err = misc_register(&sensor_device);
	if (err) {
		pr_err("yuv5 %s: Unable to register misc device\n", __func__);
		kfree(info);
		return err;
	}

	info->pdata = client->dev.platform_data;
	info->i2c_client = client;

	i2c_set_clientdata(client, info);

#ifdef CONFIG_VIDEO_LTC3216
	torch_off_wq = create_singlethread_workqueue("torch_off_wq");
	if (!torch_off_wq)
		return -ENOMEM;
	INIT_WORK(&info->work, torch_off_work_func);
	setup_timer(&(info->torch_off_timer), torch_off_timer_func, (unsigned long)NULL);
#endif

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct sensor_info *info;

	pr_info("yuv5 %s\n", __func__);

#ifdef CONFIG_VIDEO_LTC3216
	if (torch_off_wq)
		destroy_workqueue(torch_off_wq);
#endif

	info = i2c_get_clientdata(client);
	misc_deregister(&sensor_device);
	kfree(info);

	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{ SENSOR_5M_NAME, 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_i2c_driver = {
	.driver = {
		.name = SENSOR_5M_NAME,
		.owner = THIS_MODULE,
	},
	.probe = sensor_probe,
	.remove = sensor_remove,
	.id_table = sensor_id,
};

static int __init sensor_init(void)
{
	pr_info("yuv5 %s\n", __func__);
	return i2c_add_driver(&sensor_i2c_driver);
}

static void __exit sensor_exit(void)
{
	pr_info("yuv5 %s\n", __func__);
	i2c_del_driver(&sensor_i2c_driver);
}

module_init(sensor_init);
module_exit(sensor_exit);
