#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/platform_device.h>
#include <linux/switch.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/input.h>

#define DRIVER_NAME             "acer-dock"
#define DOCK_KEY_MUTE           "mute"
#define DOCK_KEY_VOLUMEUP       "vup"
#define DOCK_KEY_VOLUMEDOWN     "vdown"
#define DOCK_KEY_PLAYPAUSE      "playpause"
#define DOCK_KEY_NEXTSONG       "next"
#define DOCK_KEY_PREVIOUSSONG   "previous"
#define DOCK_KEY_BACK           "back"
#define DOCK_KEY_MENU           "menu"
#define DOCK_KEY_HOME           "home"
#define DOCK_KEY_UP             "up"
#define DOCK_KEY_DOWN           "down"
#define DOCK_KEY_LEFT           "left"
#define DOCK_KEY_RIGHT          "right"
#define DOCK_KEY_ENTER          "enter"

/* Dock Headset */
#define DOCK_HS_GPIO    190 //TEGRA_GPIO_PX6

static void dock_hs_switch_work(struct work_struct *work);

struct dock_switch_data *switch_data;

enum {
	NO_DEVICE = 0,
	DOCK_IN = 1,
	DOCK_HEADSET_IN = 2
};

struct input_dev *input_dock;

static ssize_t dock_show(struct kobject* kobj, struct kobj_attribute* kobj_attr, char* buf);
static ssize_t dock_store(struct kobject* kobj, struct kobj_attribute* kobj_attr, char* buf, size_t n);

static struct kobject *dock_kobj;

#define SYS_ATTR(_name) \
	static struct kobj_attribute _name##_attr = { \
	.attr = { \
	.name = __stringify(_name), \
	.mode = S_IRUGO|S_IWUSR, \
	}, \
	.show = _name##_show, \
	.store = _name##_store, \
	}

SYS_ATTR(dock);

static struct attribute * attributes[] = {
	&dock_attr.attr,
        NULL,
};

static struct attribute_group attr_group = {
        .attrs = attributes,
};

struct dock_switch_data {
	struct switch_dev sdev;
	unsigned gpio;
	unsigned irq;
	struct work_struct work;
	unsigned hs_gpio;
	unsigned hs_irq;
	struct work_struct hs_work;
	struct hrtimer timer;
	ktime_t hs_debounce_time;
};

static ssize_t dock_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "test doc\n");
	return (s - buf);
}

static ssize_t dock_store(struct kobject* kobj, struct kobj_attribute* kobj_attr, char* buf, size_t n)
{
	if(n >= strlen(DOCK_KEY_MUTE) && strncmp(buf, DOCK_KEY_MUTE, strlen(DOCK_KEY_MUTE)) == 0)
	{
		input_report_key( input_dock, KEY_MUTE, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_MUTE, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_VOLUMEUP) && strncmp(buf, DOCK_KEY_VOLUMEUP, strlen(DOCK_KEY_VOLUMEUP)) == 0)
	{
		input_report_key( input_dock, KEY_VOLUMEUP, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_VOLUMEUP, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_VOLUMEDOWN) && strncmp(buf, DOCK_KEY_VOLUMEDOWN, strlen(DOCK_KEY_VOLUMEDOWN)) == 0)
	{
		input_report_key( input_dock, KEY_VOLUMEDOWN, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_VOLUMEDOWN, 0);
		input_sync(input_dock );
		return n;
	}


	if(n >= strlen(DOCK_KEY_PLAYPAUSE) && strncmp(buf, DOCK_KEY_PLAYPAUSE, strlen(DOCK_KEY_PLAYPAUSE)) == 0)
	{
		input_report_key( input_dock, KEY_PLAYPAUSE, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_PLAYPAUSE, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_NEXTSONG) && strncmp(buf, DOCK_KEY_NEXTSONG, strlen(DOCK_KEY_NEXTSONG)) == 0)
	{
		input_report_key( input_dock, KEY_NEXTSONG, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_NEXTSONG, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_PREVIOUSSONG) && strncmp(buf, DOCK_KEY_PREVIOUSSONG, strlen(DOCK_KEY_PREVIOUSSONG)) == 0)
	{
		input_report_key( input_dock, KEY_PREVIOUSSONG, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_PREVIOUSSONG, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_BACK) && strncmp(buf, DOCK_KEY_BACK, strlen(DOCK_KEY_BACK)) == 0)
	{
		input_report_key( input_dock, KEY_BACK, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_BACK, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_MENU) && strncmp(buf, DOCK_KEY_MENU, strlen(DOCK_KEY_MENU)) == 0)
	{
		input_report_key( input_dock, KEY_MENU, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_MENU, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_HOME) && strncmp(buf, DOCK_KEY_HOME, strlen(DOCK_KEY_HOME)) == 0)
	{
		input_report_key( input_dock, KEY_HOME, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_HOME, 0);
		input_sync(input_dock );
		return n;
	}


	if(n >= strlen(DOCK_KEY_LEFT) && strncmp(buf, DOCK_KEY_LEFT, strlen(DOCK_KEY_LEFT)) == 0)
	{
		input_report_key( input_dock, KEY_LEFT, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_LEFT, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_RIGHT) && strncmp(buf, DOCK_KEY_RIGHT, strlen(DOCK_KEY_RIGHT)) == 0)
	{
		input_report_key( input_dock, KEY_RIGHT, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_RIGHT, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_UP) && strncmp(buf, DOCK_KEY_UP, strlen(DOCK_KEY_UP)) == 0)
	{
		input_report_key( input_dock, KEY_UP, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_UP, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_DOWN) && strncmp(buf, DOCK_KEY_DOWN, strlen(DOCK_KEY_DOWN)) == 0)
	{
		input_report_key( input_dock, KEY_DOWN, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_DOWN, 0);
		input_sync(input_dock );
		return n;
	}

	if(n >= strlen(DOCK_KEY_ENTER) && strncmp(buf, DOCK_KEY_ENTER, strlen(DOCK_KEY_ENTER)) == 0)
	{
		input_report_key( input_dock, KEY_ENTER, 1);
		input_sync( input_dock );
		input_report_key( input_dock, KEY_ENTER, 0);
		input_sync(input_dock );
		return n;
	}

	printk("wrong parameters\n");

	return n;
}

static ssize_t switch_print_name(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "%s\n", DRIVER_NAME);
}

static ssize_t switch_print_state(struct switch_dev *sdev, char *buf)
{
	switch (switch_get_state(sdev)) {
		case NO_DEVICE:
			return sprintf(buf, "%s\n", "0");
		case DOCK_IN:
			return sprintf(buf, "%s\n", "1");
		case DOCK_HEADSET_IN:
			return sprintf(buf, "%s\n", "2");
	}
	return -EINVAL;
}


static void dock_switch_work(struct work_struct *work)
{
	int state;
	struct dock_switch_data *pSwitch = switch_data;

	state = gpio_get_value(pSwitch->gpio);
	printk("dock_switch gpio=%d\n",state);

	switch_set_state(&pSwitch->sdev, state);
	hrtimer_start(&pSwitch->timer, pSwitch->hs_debounce_time, HRTIMER_MODE_REL);
}

/* Dock Headset */
static void dock_hs_switch_work(struct work_struct *work)
{
	int state,hs_state;
	struct dock_switch_data *pSwitch = switch_data;

	state = gpio_get_value(pSwitch->gpio);

	if (state) {
		hs_state = gpio_get_value(pSwitch->hs_gpio);
		printk("dock_switch hs_gpio=%d\n",hs_state);
		if (hs_state) {
			switch_set_state(&pSwitch->sdev, DOCK_HEADSET_IN);
		} else {
			switch_set_state(&pSwitch->sdev, DOCK_IN);
		}
	}
}

static enum hrtimer_restart detect_event_timer_func(struct hrtimer *data)
{
	struct dock_switch_data *pSwitch = switch_data;

	schedule_work(&pSwitch->hs_work);
	return HRTIMER_NORESTART;
}
/* Dock Headset*/

static irqreturn_t dock_interrupt(int irq, void *dev_id)
{
	struct dock_switch_data *pSwitch = switch_data;

	schedule_work(&pSwitch->work);

	return IRQ_HANDLED;
}

static irqreturn_t dock_hs_interrupt(int irq, void *dev_id)
{
	struct dock_switch_data *pSwitch = switch_data;

	hrtimer_start(&pSwitch->timer, pSwitch->hs_debounce_time, HRTIMER_MODE_REL);

	return IRQ_HANDLED;
}

static int __init dock_register_input(struct input_dev *input)
{
	input->name = DRIVER_NAME;
	input->evbit[0] = BIT_MASK(EV_SYN)|BIT_MASK(EV_KEY);
	input->keybit[BIT_WORD(KEY_MUTE)] |= BIT_MASK(KEY_MUTE);
	input->keybit[BIT_WORD(KEY_VOLUMEUP)] |= BIT_MASK(KEY_VOLUMEUP);
	input->keybit[BIT_WORD(KEY_VOLUMEDOWN)] |= BIT_MASK(KEY_VOLUMEDOWN);
	input->keybit[BIT_WORD(KEY_PLAYPAUSE)] |= BIT_MASK(KEY_PLAYPAUSE);
	input->keybit[BIT_WORD(KEY_NEXTSONG)] |= BIT_MASK(KEY_NEXTSONG);
	input->keybit[BIT_WORD(KEY_PREVIOUSSONG)] |= BIT_MASK(KEY_PREVIOUSSONG);
	input->keybit[BIT_WORD(KEY_BACK)] |= BIT_MASK(KEY_BACK);
	input->keybit[BIT_WORD(KEY_MENU)] |= BIT_MASK(KEY_MENU);
	input->keybit[BIT_WORD(KEY_HOME)] |= BIT_MASK(KEY_HOME);
	input->keybit[BIT_WORD(KEY_UP)] |= BIT_MASK(KEY_UP);
	input->keybit[BIT_WORD(KEY_DOWN)] |= BIT_MASK(KEY_DOWN);
	input->keybit[BIT_WORD(KEY_LEFT)] |= BIT_MASK(KEY_LEFT);
	input->keybit[BIT_WORD(KEY_RIGHT)] |= BIT_MASK(KEY_RIGHT);
	input->keybit[BIT_WORD(KEY_ENTER)] |= BIT_MASK(KEY_ENTER);

	return input_register_device(input);
}

static int dock_switch_probe(struct platform_device *pdev)
{
	struct gpio_switch_platform_data *pdata = pdev->dev.platform_data;

	int ret = -EBUSY;
	int err;

	if (!pdata)
		return -EBUSY;

	switch_data = kzalloc(sizeof(struct dock_switch_data), GFP_KERNEL);
	if (!switch_data)
		return -ENOMEM;

	switch_data->gpio = pdata->gpio;
	switch_data->hs_gpio = DOCK_HS_GPIO;
	switch_data->irq = gpio_to_irq(pdata->gpio);
	switch_data->hs_irq = gpio_to_irq(DOCK_HS_GPIO);
	switch_data->sdev.print_state = switch_print_state;
	switch_data->sdev.name = DRIVER_NAME;
	switch_data->sdev.print_name = switch_print_name;
	switch_data->sdev.print_state = switch_print_state;
	ret = switch_dev_register(&switch_data->sdev);
	if (ret < 0)
		goto err_register_switch;

	INIT_WORK(&switch_data->work, dock_switch_work);

	ret = request_irq(switch_data->irq, dock_interrupt,
		IRQF_DISABLED | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		DRIVER_NAME, switch_data);
	if (ret) {
		pr_err("dock_switch request irq failed\n");
		goto err_request_irq;
	}

	input_dock = input_allocate_device();
	if (input_dock == NULL) {
		pr_err("dock input_allocate_device error!\n");
		ret = -ENOMEM;
		goto err_alloc_input_dev;
	}

	err = dock_register_input(input_dock);
	if (err < 0) {
		pr_err("dock register_input error\n");
		goto err_register_input_dev;
	}

	/* Dock Headset */
	INIT_WORK(&switch_data->hs_work, dock_hs_switch_work);
	switch_data->hs_debounce_time = ktime_set(0, 250000000);
	hrtimer_init(&switch_data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	switch_data->timer.function = detect_event_timer_func;

	ret = request_irq(switch_data->hs_irq, dock_hs_interrupt,
		IRQF_DISABLED | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		"dock_hs", switch_data);
	if (ret) {
		pr_err("dock_switch request hs_irq failed\n");
		goto err_request_hs_det_irq;
	}

	dock_kobj = kobject_create_and_add("dock", NULL);
	if (dock_kobj == NULL)
		pr_err("%s: subsystem_register failed\n", __FUNCTION__);
	err = sysfs_create_group(dock_kobj, &attr_group);
	if (err)
		pr_err("%s: sysfs_create_group failed, %d\n", __FUNCTION__, __LINE__);

	// set current status
	dock_switch_work(&switch_data->work);

	printk(KERN_INFO "Dock switch driver probe done!\n");
	return 0;

err_request_hs_det_irq:
	input_unregister_device(input_dock);
err_register_input_dev:
	input_free_device(input_dock);
err_alloc_input_dev:
	free_irq(switch_data->irq, switch_data);
err_request_irq:
	switch_dev_unregister(&switch_data->sdev);
err_register_switch:
	kfree(switch_data);
	return ret;
}

static int __devexit dock_switch_remove(struct platform_device *pdev)
{
	struct dock_switch_data *switch_data = platform_get_drvdata(pdev);

	cancel_work_sync(&switch_data->work);
	switch_dev_unregister(&switch_data->sdev);
	kfree(switch_data);

	return 0;
}

static int dock_switch_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int dock_switch_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver dock_switch_driver = {
	.probe      = dock_switch_probe,
	.remove     = __devexit_p(dock_switch_remove),
	.suspend    = dock_switch_suspend,
	.resume     = dock_switch_resume,
	.driver     = {
		.name   = DRIVER_NAME,
		.owner  = THIS_MODULE,
	},
};

static int __init dock_switch_init(void)
{
	int err;

	err = platform_driver_register(&dock_switch_driver);
	if (err)
		goto err_exit;

	return 0;

err_exit:
	printk(KERN_INFO "Dock Switch register Failed! ----->>>\n");
	return err;
}

static void __exit dock_switch_exit(void)
{
	platform_driver_unregister(&dock_switch_driver);
}

module_init(dock_switch_init);
module_exit(dock_switch_exit);

MODULE_DESCRIPTION("Dock Switch Driver");
MODULE_LICENSE("GPL");
