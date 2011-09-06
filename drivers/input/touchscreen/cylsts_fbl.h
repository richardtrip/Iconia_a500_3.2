/*
 * File Based Loader Header for:
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
#include <linux/ctype.h>

/* Turn on verbose debug statements */
/*
#define CY_VERBOSE
 */
#define CY_VERBOSE
#define CY_MAX_STATUS_SIZE 32

struct cyttsp_fbl {
	bool fw_fbl_mode;
	bool suspended;
	bool stat_rdy;
	int stat_size;
	u8 stat_buf[CY_MAX_STATUS_SIZE];
	u8 reg_id;
};

static int cyttsp_wr_reg(struct cyttsp *ts, u8 reg_id, u8 reg_data)
{
	return ttsp_write_block_data(ts,
		CY_REG_BASE + reg_id, sizeof(u8), &reg_data, 0x67, true);
}

static int cyttsp_rd_reg(struct cyttsp *ts, u8 reg_id, u8 *reg_data)
{
	return ttsp_read_block_data(ts,
		CY_REG_BASE + reg_id, sizeof(u8), reg_data, 0x67, true);
}

#define CY_COMM_BUSY	0xFF
#define CY_CMD_BUSY	0xFE
static bool cyttsp_get_status(struct cyttsp *ts, int size, int delay,
	int addr, u8 *buf)
{
	int tries;
	int retval;

	if (size) {
		tries = 0;
		do {
			mdelay(delay);
			retval =  ttsp_read_block_data(ts, CY_REG_BASE,
				size, buf, addr, false);
		} while (((retval < 0) ||
			(!(retval < 0) &&
			((buf[1] == CY_COMM_BUSY) ||
			(buf[1] == CY_CMD_BUSY)))) &&
			(tries++ < CY_DELAY_MAX));

	#ifdef CY_VERBOSE
		dev_dbg(ts->dev, "%s: tries=%d ret=%d status=%02X\n",
			__func__, tries, retval, buf[1]);
	#endif

		if (retval < 0)
			return false;
		else
			return true;
	} else
		return false;
}

static ssize_t firmware_write(struct file* file, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t pos, size_t cmd_size)
{
	unsigned short val;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct cyttsp *ts = dev_get_drvdata(dev);
	mutex_lock(&(ts->mutex));
	if (!((struct cyttsp_fbl *)(ts->fw_fbl))->fw_fbl_mode) {
		val = *(unsigned short *)buf;
		((struct cyttsp_fbl *)(ts->fw_fbl))->reg_id =
			(val & 0xff00) >> 8;
		if (!(((struct cyttsp_fbl *)(ts->fw_fbl))->reg_id & 0x80)) {
			/* write user specified operational register */
			cyttsp_wr_reg(ts,
				((struct cyttsp_fbl *)(ts->fw_fbl))->reg_id,
				(u8)(val & 0xff));
			dev_dbg(ts->dev, "%s: write(r=0x%02X d=0x%02X)\n",
				__func__,
				((struct cyttsp_fbl *)(ts->fw_fbl))->reg_id,
				(val & 0xff));
		} else {
			/* save user specified operational read register */
			dev_dbg(ts->dev, "%s: read(r=0x%02X)\n",
				__func__,
				((struct cyttsp_fbl *)(ts->fw_fbl))->reg_id);
		}
	} else {
		int retval = 0;
		int tries;
		u8 i2c_addr = buf[0];
		u8 status_size = buf[1];
		u8 delay = buf[2];
		#ifdef CY_VERBOSE
		#ifdef DEBUG
			char str[128];
			char *p = str;
			int i;
			for (i = 0; i < cmd_size; i++, p += 2)
				sprintf(p, "%02x", (unsigned char)buf[i]);
			dev_dbg(ts->dev, "%s: size %d, pos %ld buf %s\n",
			       __func__, cmd_size, (long)pos, str);
		#endif
		#endif
		if (buf[4] == 0x35)
			dev_info(ts->dev, "%s: acquire PSOC1 chip=%d\n",
				__func__, buf[7]);
		tries = 0;
		do {
			retval = ttsp_write_block_data(ts, CY_REG_BASE,
				cmd_size - 3, buf + 3, i2c_addr, false);
			if (retval < 0)
				msleep(100);
		} while ((retval < 0) && (tries++ < 10));
		memset(((struct cyttsp_fbl *)(ts->fw_fbl))->stat_buf,
			0, CY_MAX_STATUS_SIZE);
		((struct cyttsp_fbl *)(ts->fw_fbl))->stat_size = status_size;
		((struct cyttsp_fbl *)(ts->fw_fbl))->stat_rdy =
			cyttsp_get_status(ts, status_size, delay, i2c_addr,
			((struct cyttsp_fbl *)(ts->fw_fbl))->stat_buf);
	}
	mutex_unlock(&(ts->mutex));
	return cmd_size;
}

static ssize_t firmware_read(struct file* file, struct kobject *kobj,
	struct bin_attribute *ba,
	char *buf, loff_t pos, size_t size)
{
	int count = 0;
	u8 reg_data;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct cyttsp *ts = dev_get_drvdata(dev);

	#ifdef CY_VERBOSE
	dev_dbg(ts->dev, "%s: Enter (mode=%d)\n",
		__func__, ((struct cyttsp_fbl *)(ts->fw_fbl))->fw_fbl_mode);
	#endif

	mutex_lock(&(ts->mutex));
	if (!((struct cyttsp_fbl *)(ts->fw_fbl))->fw_fbl_mode) {
		/* read user specified operational register */
		cyttsp_rd_reg(ts,
			((struct cyttsp_fbl *)(ts->fw_fbl))->reg_id & ~0x80,
			&reg_data);
		*(unsigned short *)buf = reg_data << 8;
		count = sizeof(unsigned short);
		dev_dbg(ts->dev, "%s: read(d=0x%02X)\n",
			__func__, reg_data);
	} else {
		if (((struct cyttsp_fbl *)(ts->fw_fbl))->stat_rdy) {
			for (count = 0; count <
				((struct cyttsp_fbl *)(ts->fw_fbl))->stat_size;
				) {
				buf[count] = ((struct cyttsp_fbl *)
					(ts->fw_fbl))->stat_buf[count];
				count++;
			}
			((struct cyttsp_fbl *)(ts->fw_fbl))->stat_rdy = false;
		} else
			count = 0;

	#ifdef CY_VERBOSE
		if (count > 1)
			dev_dbg(ts->dev, "%s: status=%02X\n", __func__, buf[1]);
	#endif
	}
	mutex_unlock(&(ts->mutex));
	return count;
}

static struct bin_attribute cyttsp_firmware = {
	.attr = {
		.name = "firmware",
		.mode = 0644,
	},
	.size = 128,
	.read = firmware_read,
	.write = firmware_write,
};

static ssize_t attr_fwloader_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	return sprintf(buf, "0x%02X%02X 0x%02X%02X 0x%02X%02X 0x%02X%02X%02X\n",
		ts->sysinfo_data.tts_verh, ts->sysinfo_data.tts_verl,
		ts->sysinfo_data.app_idh, ts->sysinfo_data.app_idl,
		ts->sysinfo_data.app_verh, ts->sysinfo_data.app_verl,
		ts->sysinfo_data.cid[0], ts->sysinfo_data.cid[1],
		ts->sysinfo_data.cid[2]);
}

enum cyttsp_fbl_mode {
	CY_FBL_CLOSE_MODE = 0,
	CY_FBL_OPEN_MODE = 1,
	CY_REG_OPEN_MODE = 2,
	CY_REG_CLOSE_MODE = 3,
};

#define CY_BUS_TMO	(-5)

static ssize_t attr_fwloader_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	char *p;
	int ret;
	u8 status_buf[32];
	struct cyttsp *ts = dev_get_drvdata(dev);
	unsigned val = simple_strtoul(buf, &p, 10);

	ret = p - buf;
	if (*p && isspace(*p))
		ret++;

	#ifdef CY_VERBOSE
	dev_dbg(ts->dev, "%s: %u\n", __func__, val);
	#endif

	mutex_lock(&(ts->mutex));
	if (val == CY_REG_CLOSE_MODE) {
		sysfs_remove_bin_file(&dev->kobj, &cyttsp_firmware);
		dev_dbg(ts->dev,  "%s: FW loader closed for reg r/w\n",
			__func__);
	} else if (val == CY_REG_OPEN_MODE) {
		if (sysfs_create_bin_file(&dev->kobj, &cyttsp_firmware))
			printk(KERN_ERR "%s: unable to create file\n",
				__func__);
		else {
			dev_dbg(ts->dev, "%s: FW ldr opened for reg r/w\n",
				__func__);
		}
	} else if ((val == CY_FBL_OPEN_MODE) &&
		!((struct cyttsp_fbl *)(ts->fw_fbl))->fw_fbl_mode) {
		if (sysfs_create_bin_file(&dev->kobj, &cyttsp_firmware)) {
				((struct cyttsp_fbl *)
				(ts->fw_fbl))->fw_fbl_mode = false;
			printk(KERN_ERR "%s: unable to create file\n",
				__func__);
		} else {
			if (ts->power_state == CY_SLEEP_STATE) {
				if (cyttsp_resume(ts))
					((struct cyttsp_fbl *)
					(ts->fw_fbl))->suspended = false;
				else
					((struct cyttsp_fbl *)
					(ts->fw_fbl))->suspended = true;
			}
			ts->power_state = CY_BL_STATE;
			((struct cyttsp_fbl *)
				(ts->fw_fbl))->fw_fbl_mode = true;

			memset(status_buf, 0, sizeof(buf));
			ret = cyttsp_enter_bl_mode(ts);
			dev_dbg(ts->dev, "%s: enter bl ret=%d\n",
				__func__, ret);

			if ((ret == 0) || (ret == CY_BUS_TMO)) {
				ts->power_state = CY_BL_STATE;
				printk(KERN_INFO "%s: FW loader started.\n",
					__func__);
				dev_dbg(ts->dev,
					"%s: FW loader opened for start load:"
					" ps=%d mode=%d ret=%d\n",
					__func__,
					ts->power_state, ((struct cyttsp_fbl *)
					(ts->fw_fbl))->fw_fbl_mode, ret);
				ret = size;
			} else {
				ts->power_state = CY_ACTIVE_STATE;
				printk(KERN_INFO "%s: FW loader fail start.\n",
					__func__);
				dev_dbg(ts->dev,
					"%s: FW loader unable to start load:"
					"ps=%d mode=%d\n",
					__func__,
					ts->power_state, ((struct cyttsp_fbl *)
					(ts->fw_fbl))->fw_fbl_mode);
			}
		}
	} else if ((val == CY_FBL_CLOSE_MODE) &&
		((struct cyttsp_fbl *)(ts->fw_fbl))->fw_fbl_mode) {
		sysfs_remove_bin_file(&dev->kobj, &cyttsp_firmware);
		dev_dbg(ts->dev,  "%s: FW loader closed\n",
			__func__);
		if (((struct cyttsp_fbl *)(ts->fw_fbl))->suspended) {
			if (cyttsp_suspend(ts)) {
				printk(KERN_ERR "%s: "
				"unable to re-enter suspend\n",
				__func__);
				ts->power_state = CY_ACTIVE_STATE;
			} else
				ts->power_state = CY_SLEEP_STATE;
		} else {
			ts->power_state = CY_ACTIVE_STATE;
		}
		printk(KERN_INFO "%s: FW loader finished.\n", __func__);
		((struct cyttsp_fbl *)(ts->fw_fbl))->fw_fbl_mode = false;
		ret = size;
	}
	mutex_unlock(&(ts->mutex));
	cyttsp_pr_state(ts);
	return  ret == size ? ret : -EINVAL;
}

static struct device_attribute fwloader =
	__ATTR(fwloader, 0644, attr_fwloader_show, attr_fwloader_store);

static void *cyttsp_fbl_init(struct cyttsp *ts)
{
	pr_info("%s: enter fbl init\n", __func__);
	ts->fw_fbl =
		kzalloc(sizeof(struct cyttsp_fbl), GFP_KERNEL);

	if (ts->fw_fbl) {
		((struct cyttsp_fbl *)(ts->fw_fbl))->fw_fbl_mode = false;
		((struct cyttsp_fbl *)(ts->fw_fbl))->suspended = false;
		((struct cyttsp_fbl *)(ts->fw_fbl))->reg_id = -1;
		((struct cyttsp_fbl *)(ts->fw_fbl))->stat_size = 0;
		((struct cyttsp_fbl *)(ts->fw_fbl))->stat_rdy = false;
		dev_dbg(ts->dev, "%s: fbl mode=%d suspended=%d reg_id=%d\n",
			__func__,
			((struct cyttsp_fbl *)(ts->fw_fbl))->fw_fbl_mode,
			((struct cyttsp_fbl *)(ts->fw_fbl))->suspended,
			((struct cyttsp_fbl *)(ts->fw_fbl))->reg_id);
		if (device_create_file(ts->dev, &fwloader)) {
			printk(KERN_ERR "%s: Error, could not create attr\n",
				__func__);
			kfree(ts->fw_fbl);
		} else {
			dev_set_drvdata(ts->dev, ts);
		}
	}

	return ts->fw_fbl;
}
