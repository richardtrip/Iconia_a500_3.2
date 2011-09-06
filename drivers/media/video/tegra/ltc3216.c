/**
 * Copyright (c) 2008 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <media/ltc3216.h>

#define LTC3216_MAX_RETRIES (3)

bool flash_on;

struct ltc3216_info {
	struct i2c_client *i2c_client;
};

struct ltc3216_info *info_flash = NULL;

static int ltc3216_write(struct i2c_client *client, u16 value)
{
	int count;
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
		count = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
		if (count == ARRAY_SIZE(msg))
			return 0;
		retry++;
		pr_err("ltc3216: i2c transfer failed, retrying %x\n",
		       value);
		msleep(3);
	} while (retry <= LTC3216_MAX_RETRIES);
	return -EIO;
}


static long ltc3216_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct ltc3216_info *info_flash = file->private_data;

	switch (cmd) {
	case LTC3216_IOCTL_FLASH_ON:
		ltc3216_write(info_flash->i2c_client, 0x90);
		ltc3216_write(info_flash->i2c_client, (u16) arg);
		break;

	case LTC3216_IOCTL_FLASH_OFF:
		ltc3216_write(info_flash->i2c_client, 0x00);
		ltc3216_write(info_flash->i2c_client, 0x104);
		break;

	case LTC3216_IOCTL_TORCH_ON:
		flash_on = true;
		ltc3216_write(info_flash->i2c_client, (u16) arg);
		break;

	case LTC3216_IOCTL_TORCH_OFF:
		flash_on = false;
		ltc3216_write(info_flash->i2c_client, 0x00);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int ltc3216_open(struct inode *inode, struct file *file)
{

	file->private_data = info_flash;
	return 0;
}

int ltc3216_release(struct inode *inode, struct file *file)
{
	if (flash_on)
		ltc3216_write(info_flash->i2c_client, 0x00);
	file->private_data = NULL;
	return 0;
}


static const struct file_operations ltc3216_fileops = {
	.owner = THIS_MODULE,
	.open = ltc3216_open,
	.unlocked_ioctl = ltc3216_ioctl,
	.release = ltc3216_release,
};

static struct miscdevice ltc3216_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ltc3216",
	.fops = &ltc3216_fileops,
};

static int ltc3216_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;

	pr_info("ltc3216: probing sensor.\n");

	info_flash = kzalloc(sizeof(struct ltc3216_info), GFP_KERNEL);
	if (!info_flash) {
		pr_err("ltc3216: Unable to allocate memory!\n");
		return -ENOMEM;
	}

	err = misc_register(&ltc3216_device);
	if (err) {
		pr_err("ltc3216: Unable to register misc device!\n");
		kfree(info_flash);
		return err;
	}

	info_flash->i2c_client = client;
	return 0;
}

static int ltc3216_remove(struct i2c_client *client)
{
	struct ltc3216_info *info_flash;
	info_flash = i2c_get_clientdata(client);
	misc_deregister(&ltc3216_device);
	kfree(info_flash);
	return 0;
}

static const struct i2c_device_id ltc3216_id[] = {
	{ "ltc3216", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, ltc3216_id);

static struct i2c_driver ltc3216_i2c_driver = {
	.driver = {
		.name = "ltc3216",
		.owner = THIS_MODULE,
	},
	.probe = ltc3216_probe,
	.remove = ltc3216_remove,
	.id_table = ltc3216_id,
};

static int __init ltc3216_init(void)
{
	pr_info("ltc3216 sensor driver loading\n");
	return i2c_add_driver(&ltc3216_i2c_driver);
}

static void __exit ltc3216_exit(void)
{
	i2c_del_driver(&ltc3216_i2c_driver);
}

void ltc3216_turn_on_flash(void)
{
	pr_info("%s() call\n", __func__);
	// set STIM to 11111b, Timer = STIM x 32.8ms = 1.02s
	ltc3216_write(info_flash->i2c_client, 0x03DF);

	ltc3216_write(info_flash->i2c_client, 0x0090);
	ltc3216_write(info_flash->i2c_client, 0x018E);
}

void ltc3216_turn_off_flash(void)
{
	pr_info("%s() call\n", __func__);
	ltc3216_write(info_flash->i2c_client, 0x0000);
	ltc3216_write(info_flash->i2c_client, 0x0104);
}

void ltc3216_turn_on_torch(void)
{
	pr_info("%s() call\n", __func__);
	ltc3216_write(info_flash->i2c_client, 0x0053);
}

void ltc3216_turn_off_torch(void)
{
	pr_info("%s() call\n", __func__);
	ltc3216_write(info_flash->i2c_client, 0x0000);
}

module_init(ltc3216_init);
module_exit(ltc3216_exit);
