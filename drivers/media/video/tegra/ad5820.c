/*
 * kernel/drivers/media/video/tegra
 *
 * AD5820 focuser driver
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
#include <media/ad5820.h>

#define AD5820_MAX_RETRIES (3)

#define MOVE_STEP 10

struct ad5820_info {
	struct i2c_client *i2c_client;
	int16_t previous_pos;
};

struct ad5820_info *info;

static int ad5820_write(struct i2c_client *client, u16 value)
{
	int err;
	struct i2c_msg msg[1];
	unsigned char data[2];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) (value >> 8);
	data[1] = (u8) (value & 0xFF);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = ARRAY_SIZE(data);
	msg[0].buf = data;

	do {
		err = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
		if (err == ARRAY_SIZE(msg))
			return 0;
		retry++;
		pr_err("ad5820: i2c transfer failed, retrying %x\n",value);
		msleep(3);
	} while (retry <= AD5820_MAX_RETRIES);
	return -EIO;
}

static long ad5820_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct ad5820_info *info = file->private_data;
	u16 position;
	int16_t pos_current, pos_target;

	pr_info("ad5820: %s\n",__func__);

	switch (cmd) {
	case AD5820_IOCTL_SET_POSITION:

		if (copy_from_user(&position,
				   (const void __user *)arg,
				   sizeof(position))) {
			return -EFAULT;
		}

		pos_current = info->previous_pos;

		if (position == 0xFFFF)
			pos_target = 0;
		else
			pos_target = (int16_t)((position & 0x3FF0) >> 4);

		pr_info("%s prepare inc/dec flow, pos_current= %d, pos_target= %d\n",
			__func__, pos_current, pos_target);
		if (pos_current > pos_target)
		{
			for (; pos_current>pos_target; pos_current-=MOVE_STEP) {
				ad5820_write(info->i2c_client, ((u16)pos_current<<4));
				udelay(500);
				pr_debug("%s -10 pos_current= %d, pos_target=%d\n",
					__func__, pos_current, pos_target);
			}
		}

		else if (pos_current < pos_target)
		{
			for (; pos_current<pos_target; pos_current+=MOVE_STEP) {
				ad5820_write(info->i2c_client, ((u16)pos_current<<4));
				udelay(500);
				pr_debug("%s +10 pos_current= %d, pos_target= %d\n",
					__func__, pos_current, pos_target);
			}
		}

		ad5820_write(info->i2c_client, position);
		info->previous_pos = pos_target;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int ad5820_open(struct inode *inode, struct file *file)
{
	file->private_data = info;
	return 0;
}

int ad5820_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	ad5820_write(info->i2c_client, 0xFFFF);
	pr_info("ad5820: release af position to 0xFFFF");

	return 0;
}

static const struct file_operations ad5820_fileops = {
	.owner = THIS_MODULE,
	.open = ad5820_open,
	.unlocked_ioctl = ad5820_ioctl,
	.release = ad5820_release,
};

static struct miscdevice ad5820_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ad5820",
	.fops = &ad5820_fileops,
};

static int ad5820_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;

	pr_info("ad5820: probing focuser.\n");

	info = kzalloc(sizeof(struct ad5820_info), GFP_KERNEL);
	if (!info) {
		pr_err("ad5820: Unable to allocate memory!\n");
		return -ENOMEM;
	}

	err = misc_register(&ad5820_device);
	if (err) {
		pr_err("ad5820: Unable to register misc device!\n");
		kfree(info);
		return err;
	}

	info->i2c_client = client;
	info->previous_pos = 0;
	return 0;
}

static int ad5820_remove(struct i2c_client *client)
{
	struct ad5820_info *info;
	info = i2c_get_clientdata(client);
	misc_deregister(&ad5820_device);
	kfree(info);
	return 0;
}

static const struct i2c_device_id ad5820_id[] = {
	{ "ad5820", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, ad5820_id);

static struct i2c_driver ad5820_i2c_driver = {
	.driver = {
		.name = "ad5820",
		.owner = THIS_MODULE,
	},
	.probe = ad5820_probe,
	.remove = ad5820_remove,
	.id_table = ad5820_id,
};

static int __init ad5820_init(void)
{
	pr_info("ad5820 focuser driver loading\n");
	return i2c_add_driver(&ad5820_i2c_driver);
}

static void __exit ad5820_exit(void)
{
	i2c_del_driver(&ad5820_i2c_driver);
}

module_init(ad5820_init);
module_exit(ad5820_exit);
