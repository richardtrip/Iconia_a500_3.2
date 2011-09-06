/*
 *  Generic Light Sensor Driver
 *
 *  Copyright (c) 2004-2008
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string.h>
#define DRIVER_NAME		"al3000a_ls"

#include <linux/types.h>
#include <linux/ioctl.h>

#define LIGHTSENSOR_IOCTL_MAGIC 'l'
#define LIGHTSENSOR_IOCTL_PRIV_ENABLE	_IOWR(LIGHTSENSOR_IOCTL_MAGIC, 1, int)
#define LIGHTSENSOR_IOCTL_PRIV_DISABLE	_IOWR(LIGHTSENSOR_IOCTL_MAGIC, 2, int)

#define pr_debug	printk

struct al_data
{
    struct work_struct work;
    struct input_dev   *input_dev;
    struct i2c_client  *client;
    atomic_t           l_flag;   //report input flag
};
static struct al_data *ls;
struct work_struct        workq;

void sample_workqueue(struct work_struct *work)
{
	struct al_data *data = container_of(work, struct al_data, work);
	__u8 level;
	int ret;

	ret = i2c_smbus_read_i2c_block_data(data->client, 0x05, 1, &level);
	if (WARN_ON(ret < 0)) {
		dev_err(&data->client->dev, "error %d reading event data\n", ret);
		return;
	}

	input_report_abs(data->input_dev, ABS_MISC, level);
	input_sync(data->input_dev);

	return;
}

static ssize_t data_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	char data;
	i2c_smbus_read_i2c_block_data(ls->client, 0x05, 1, &data);
	return sprintf(buf, " light =%x\n", data);
}

static ssize_t data_store(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t count)
{
	return count;
}

static ssize_t light_lux_show(struct kobject *, struct kobj_attribute *, char *);

static ssize_t light_lux_store(struct kobject *,
				struct kobj_attribute *,
				const char *,
				size_t);

static ssize_t light_mode_show(struct kobject *, struct kobj_attribute *, char *);

static ssize_t light_mode_store(struct kobject *,
				struct kobj_attribute *,
				const char *,
				size_t);

static void ls_enable(int);

static ssize_t light_lux_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	char lux = '0';
	i2c_smbus_read_i2c_block_data(ls->client, 0x05, 1, &lux);
	return sprintf(buf, "%d\n", lux);
}

static ssize_t light_lux_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	return count;
}

static ssize_t light_mode_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return sprintf(buf, "real light sensor status : %d\n(not echo 1/0 > /sys/light/light_mode status)\n",
			atomic_read(&ls->l_flag));
}

static ssize_t light_mode_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	int res = 0;
	if ( *buf == '1' && !atomic_read(&ls->l_flag)){
		ls_enable(1);
		res = strlen(buf);
	} else {
		if( !atomic_read(&ls->l_flag) ){
			ls_enable(0);
			res = strlen(buf);
		} else
			res = -EINVAL;
	}

	return res;
}

static struct kobj_attribute light_data_attribute =
	__ATTR(light, 0644, data_show, data_store);

static struct kobj_attribute light_lux_light_lux_attribute =
	__ATTR(light_lux, 0644, light_lux_show, light_lux_store);

static struct kobj_attribute light_mode_light_mode_attribute =
	__ATTR(light_mode, 0644, light_mode_show, light_mode_store);

static struct attribute *attrs[] = {
	&light_data_attribute.attr,
	&light_lux_light_lux_attribute.attr,
	&light_mode_light_mode_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *light_kobj;

static void ls_enable(int enable)
{
	struct i2c_client *client = ls->client;
	__u8 data = 0;

	if (enable) {
		pr_debug("AL3000A Light sensor enabled --->\n");
		//Read Data to clear INT Flag
		i2c_smbus_read_i2c_block_data(client, 0x05, 1, &data);
		//Power Up & Enable ALS
		i2c_smbus_write_byte_data(client, 0x00, 0x00);
	} else {
		pr_debug("AL3000A Light sensor disabled --->\n");
		//Power Down & Idle
		i2c_smbus_write_byte_data(client, 0x00, 0x0B);
	}
	//atomic_set(&ls->l_flag, enable);
}

static int light_misc_open(struct inode *inode, struct file *file)
{
	pr_debug("%s: ---------->>>\n", __func__);
	return 0;
}

static int light_misc_release(struct inode *inode, struct file *file)
{
	pr_debug("%s: ---------->>>\n", __func__);
	return 0;
}

static int
light_misc_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int flag = 0;

	switch (cmd) {
		case LIGHTSENSOR_IOCTL_PRIV_ENABLE:
			ls_enable(1);
			atomic_set(&ls->l_flag, 1);
		break;
		case LIGHTSENSOR_IOCTL_PRIV_DISABLE:
			ls_enable(0);
			atomic_set(&ls->l_flag, 0);
		break;
		default:
			pr_err("%s: miss ioctl cmd:0x%X\n", __func__, cmd);
			return -ENOTTY;
		break;
	}

	return 0;
}

static struct file_operations light_misc_fops = {
	.owner = THIS_MODULE,
	.open = light_misc_open,
	.release = light_misc_release,
	.unlocked_ioctl = light_misc_ioctl,
};

static struct miscdevice light_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &light_misc_fops,
};

static int al_init(struct i2c_client *client)
{
	__u8 data;
	int ret = 0;

	//F/W Initial Flow
	//Power Down & Idle
	ret = i2c_smbus_write_byte_data(client, 0x00, 0x0B);
	if (ret < 0) {
		pr_err("Init AL3000A failed - 1\n");
		return ret;
	}

	// Integration Cycle = 4; Integration Time = 100ms;
	// Interrupt trigger when lux detection has changed 4 times
	// at 100ms intervals.
	ret = i2c_smbus_write_byte_data(client, 0x01, 0x11);
	if (ret < 0) {
		pr_err("Init AL3000A failed - 2\n");
		return ret;
	}

	//AL3000A ADC resolution = 64 levels; Low lux threshold = 0
	ret = i2c_smbus_write_byte_data(client, 0x02, 0xA0);
	if (ret < 0) {
		pr_err("Init AL3000A failed - 3\n");
		return ret;
	}

	//ALS Window Loss = 0
	//It isn't covered by shell so no window loss, need to modify at DVT
	ret = i2c_smbus_write_byte_data(client, 0x08, 0x00);
	if (ret < 0) {
		pr_err("Init AL3000A failed - 4\n");
		return ret;
	}

	//Read Data to clear INT Flag
	ret = i2c_smbus_read_i2c_block_data(client, 0x05, 1, &data);
	if (ret < 0) {
		pr_err("Init AL3000A failed - 5\n");
		return ret;
	}

	return ret;
}

static irqreturn_t al3000a_irq(int irq, void *dev_id)
{
	struct al_data *data = dev_id;
	schedule_work(&data->work);
	return IRQ_HANDLED;
}

static int al3000a_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct al_data *als = NULL;
	int ret = 0;

	if (al_init(client) < 0)
		return -EINVAL;

	als = kzalloc(sizeof(struct al_data), GFP_KERNEL);
	if (!als) {
	        pr_err("%s: no memory\n", __func__);
		return -ENOMEM;
	}

	/* Declare input device */
	als->input_dev = input_allocate_device();
	if (!als->input_dev) {
		pr_err("%s: failed to allocate input device\n", __func__);
	        kfree(als);
		return -ENOMEM;
	}

	/* Setup input device */
	set_bit(EV_ABS, als->input_dev->evbit);
	input_set_abs_params(als->input_dev, ABS_MISC, 0, 0xFF, 0, 0);

	als->client = client;
	i2c_set_clientdata(client, als);

	/* Set name */
	als->input_dev->name = "lightsensor-level";
	/* Register */
	ret = input_register_device(als->input_dev);
	if (ret) {
		pr_err("%s: input_register_device failed\n", __func__);
		goto fail_input_device;
	}

	/* Setup misc device */
	ret = misc_register(&light_misc_device);
	if (ret) {
		pr_err("%s: misc_register failed\n", __func__);
		goto fail_misc_device;
	}

	/* As default, doesn't report input event */
	atomic_set(&als->l_flag, 0);

	ls = als;

	INIT_WORK(&als->work,sample_workqueue);

	/* get the irq */
	ret = request_irq(client->irq,al3000a_irq,IRQF_TRIGGER_FALLING,
			DRIVER_NAME, als);

	//ls_enable(1);
	//schedule_work(&als->work);
	if (ret) {
		pr_err("%s: request_irq(%d) failed\n",
			__func__, als->client->irq);
		goto fail_request_irq;
	}

	light_kobj = kobject_create_and_add("light", NULL);
	if (!light_kobj)
		return -ENOMEM;

	/* Create the files associated with this kobject */
	ret = sysfs_create_group(light_kobj, &attr_group);
	if (ret)
		kobject_put(light_kobj);

	pr_info("AL3000A Light Sensor Driver Initialized");

	return 0;

fail_request_irq:
	misc_deregister(&light_misc_device);
fail_misc_device:
	input_unregister_device(als->input_dev);
fail_input_device:
	input_free_device(als->input_dev);
	kfree(als);

	return ret;
}

static int al3000a_remove(struct i2c_client *client)
{
	struct al_data *data = i2c_get_clientdata(client);

	pr_info("AL3000A Light Sensor Unloaded\n");

	if (!data)
		return -EINVAL;

	cancel_work_sync(&data->work);
	free_irq(data->client->irq, data);
	misc_deregister(&light_misc_device);
	input_unregister_device(data->input_dev);
	input_free_device(data->input_dev);
	kfree(data);

	return 0;
}

static int al3000a_suspend(struct i2c_client *client, pm_message_t state)
{
	struct al_data *data = i2c_get_clientdata(client);
	int ret;

	if (WARN_ON(!data))
		return -EINVAL;

	disable_irq(client->irq);

	//Power Up & Enable ALS
	ret = i2c_smbus_write_byte_data(client, 0x00, 0x0B);
	if (ret < 0) {
		pr_err("%s: suspend failed\n", __func__);
		return ret;
	}

	return 0;
}

static int al3000a_resume(struct i2c_client *client)
{
	struct al_data *data = i2c_get_clientdata(client);
	int ret = 0;

	if (WARN_ON(!data))
		return -EINVAL;

	ret = i2c_smbus_write_byte_data(client, 0x00, 0x00);
	if (ret < 0) {
		pr_err("%s: resume failed\n", __func__);
		return ret;
	}

	enable_irq(client->irq);

	return 0;
}

static const struct i2c_device_id al3000a_id[] = {
	{ DRIVER_NAME, 0 },
	{ },
};

static struct i2c_driver al3000a_driver = {
	.probe	= al3000a_probe,
	.remove	= al3000a_remove,
#if defined (CONFIG_PM)
	.suspend    = al3000a_suspend,
	.resume     = al3000a_resume,
#endif
	.id_table   = al3000a_id,
	.driver	= {
		.name = DRIVER_NAME,
	},
};

static int __devinit al3000a_init(void)
{
	int ret;

	ret = i2c_add_driver(&al3000a_driver);
	if (ret != 0) {
		pr_err("%s: failed to register with I2C bus with "
			"error: 0x%x\n", __func__, ret);
	}

	return ret;
}

static void __exit al3000a_exit(void)
{
	kobject_put(light_kobj);
	i2c_del_driver(&al3000a_driver);
}

module_init(al3000a_init);
module_exit(al3000a_exit);
MODULE_DESCRIPTION("AL3000A Light Sensor Driver");

