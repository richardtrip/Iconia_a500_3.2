#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/platform_device.h>
#include <linux/switch.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define DRIVER_NAME     "simdetect"
#define SIMDET_DEBOUNCE_TIME_MS 250

static int sim_Status = -1;

// This API is exported by EC_battery driver
// We will manually enable 3G power if no SIM card.
extern void enable_ThreeGPower(void);
extern void disable_ThreeGPower(void);

// Flag to check if it's the first run
static bool initialized = false;

#ifdef CONFIG_HAS_EARLYSUSPEND
static void func_simdet_late_resume(struct early_suspend *);
static void func_simdet_early_suspend(struct early_suspend *);

struct early_suspend simdet_early_suspend;
static int sim_Status_after_resume;
static struct simdet_switch_data *switch_data_priv;
#endif

struct simdet_switch_data {
    struct switch_dev sdev;
    unsigned gpio;
    unsigned irq;
    struct work_struct work;
    // debounce timer
    struct hrtimer timer;
    ktime_t debounce_time;
};

/*
 *              SIM_DET (GPIO_PI7)
 *     ----------------------------------
 *   __|__             ____           ___|____
 *  |     |           |    |         |        |
 *  | SoC |           | EC |<--------|SIM Slot|
 *  |_____|           |____| SIM_DET |________|
 *                       |
 *                 3G_EN#|            ________
 *                       |___________|        |
 *                      Power Switch |  WWAN  |
 *                                   |________|
 *
 * This driver is in charge of the part of sim card detection
 * and then send uevent to userspace.
 */

#ifdef CONFIG_HAS_EARLYSUSPEND
static void func_simdet_early_suspend(struct early_suspend *es)
{
    printk("%s: SimDet: early suspend, status: %d\n",
                        DRIVER_NAME,
                        sim_Status);
}

static void func_simdet_late_resume(struct early_suspend *es)
{
    sim_Status_after_resume = gpio_get_value(switch_data_priv->gpio);
    printk("%s: SimDet: late resume, status: %d\n",
                        DRIVER_NAME,
                        sim_Status_after_resume);

    if (sim_Status_after_resume != sim_Status) {
        printk("%s: Sim status changed from %d to %d\n",
                            DRIVER_NAME,
                            sim_Status,
                            sim_Status_after_resume);
	sim_Status = sim_Status_after_resume;
	switch_set_state(&switch_data_priv->sdev, sim_Status);
    }
}
#endif

static ssize_t switch_print_name(struct switch_dev *sdev, char *buf)
{
    return sprintf(buf, "%s\n", DRIVER_NAME);
}

static ssize_t switch_print_state(struct switch_dev *sdev, char *buf)
{
    return sprintf(buf, "%s\n", (switch_get_state(sdev) ? "1" : "0"));
}

static void simdet_switch_work(struct work_struct *work)
{
    struct simdet_switch_data *pSwitch =
            container_of(work, struct simdet_switch_data, work);

    int pin_val;

    pin_val = gpio_get_value(pSwitch->gpio);

    if(!initialized) {
	printk("%s: First check on SIM status\n", DRIVER_NAME);
	initialized = true;
	sim_Status = pin_val;

	if(sim_Status == 0)
	    printk("%s: SIM is not found\n", DRIVER_NAME);
	else
	    printk("%s: SIM is inserted\n", DRIVER_NAME);

	switch_set_state(&pSwitch->sdev, sim_Status);
    } else {
	if(pin_val != sim_Status) {
	    // real SIM hotplug event
	    sim_Status = pin_val;
	    printk("%s: Sim status is changed during runtime: %d\n", DRIVER_NAME, sim_Status);
	    switch_set_state(&pSwitch->sdev, sim_Status);
	} else
	    printk("%s: False alarm, SIM status isn't changed\n", DRIVER_NAME);
    }
}

static enum hrtimer_restart detect_event_timer_func(struct hrtimer *timer)
{
    struct simdet_switch_data *pSwitch =
            container_of(timer, struct simdet_switch_data, timer);

    schedule_work(&pSwitch->work);
    return HRTIMER_NORESTART;
}

static irqreturn_t simdet_interrupt(int irq, void *dev_id)
{
    struct simdet_switch_data *pSwitch = (struct simdet_switch_data *)dev_id;

    hrtimer_start(&pSwitch->timer, pSwitch->debounce_time, HRTIMER_MODE_REL);
    return IRQ_HANDLED;
}


static int simdet_switch_probe(struct platform_device *pdev)
{
    struct gpio_switch_platform_data *pdata = pdev->dev.platform_data;
    struct simdet_switch_data *switch_data;
    int ret = -EBUSY;

    if (!pdata)
        return -EBUSY;

    switch_data = kzalloc(sizeof(struct simdet_switch_data), GFP_KERNEL);
    if (!switch_data)
        return -ENOMEM;

    switch_data->gpio = pdata->gpio;
    switch_data->irq = gpio_to_irq(pdata->gpio);
    switch_data->sdev.print_state = switch_print_state;
    switch_data->sdev.name = DRIVER_NAME;
    switch_data->sdev.print_name = switch_print_name;
    ret = switch_dev_register(&switch_data->sdev);
    if (ret < 0)
        goto err_register_switch;

    ret = gpio_request(switch_data->gpio, pdev->name);
    if (ret < 0)
        goto err_request_gpio;

    ret = gpio_direction_input(switch_data->gpio);
    if (ret < 0)
        goto err_set_gpio_input;

    INIT_WORK(&switch_data->work, simdet_switch_work);

    switch_data->irq = gpio_to_irq(switch_data->gpio);
    if (switch_data->irq < 0) {
        ret = switch_data->irq;
	goto err_detect_irq_num_failed;
    }

    ret = request_irq(switch_data->irq, simdet_interrupt, IRQF_DISABLED | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                 "simdetect", switch_data);
    if(ret)
    {
	printk("simdet_switch request irq failed\n");
        goto err_request_irq;
    }

    // set current status
    simdet_switch_work(&switch_data->work);

    // hrtimer
    switch_data->debounce_time = ktime_set(0, SIMDET_DEBOUNCE_TIME_MS*1000000);
    hrtimer_init(&switch_data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    switch_data->timer.function = detect_event_timer_func;

#ifdef CONFIG_HAS_EARLYSUSPEND
    switch_data_priv = switch_data;

    simdet_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    simdet_early_suspend.suspend = func_simdet_early_suspend;
    simdet_early_suspend.resume = func_simdet_late_resume;
    register_early_suspend(&simdet_early_suspend);
#endif

    return 0;

err_request_irq:
err_detect_irq_num_failed:
err_set_gpio_input:
    free_irq(switch_data->irq, switch_data);
err_request_gpio:
    switch_dev_unregister(&switch_data->sdev);
err_register_switch:
    kfree(switch_data);
    return ret;
}

static int __devexit simdet_switch_remove(struct platform_device *pdev)
{
    struct simdet_switch_data *switch_data = platform_get_drvdata(pdev);

    cancel_work_sync(&switch_data->work);
    gpio_free(switch_data->gpio);
    switch_dev_unregister(&switch_data->sdev);
    kfree(switch_data);

    return 0;
}

#ifdef CONFIG_PM
static int simdet_switch_suspend(struct platform_device *pdev, pm_message_t state)
{
    return 0;
}

static int simdet_switch_resume(struct platform_device *pdev)
{
    return 0;
}
#endif

static struct platform_driver simdet_switch_driver = {
    .probe      = simdet_switch_probe,
    .remove     = __devexit_p(simdet_switch_remove),
#ifdef CONFIG_PM
    .suspend    = simdet_switch_suspend,
    .resume     = simdet_switch_resume,
#endif
    .driver     = {
        .name   = "simdetect",
        .owner  = THIS_MODULE,
    },
};

static int __init simdet_switch_init(void)
{
    int err;

    printk(KERN_INFO "simdet_switch initial\n");

    err = platform_driver_register(&simdet_switch_driver);
    if (err)
        goto err_exit;

    printk(KERN_INFO "simdet_switch register OK\n");

    return 0;

err_exit:
    printk(KERN_INFO "simdet_switch register Failed!\n");
    return err;
}

static void __exit simdet_switch_exit(void)
{
    printk(KERN_INFO "simdet_switch driver unregister\n");
    platform_driver_unregister(&simdet_switch_driver);
}

module_init(simdet_switch_init);
module_exit(simdet_switch_exit);

MODULE_DESCRIPTION("SimCard Detection Switch Driver");
MODULE_LICENSE("GPL");
