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

#define DRIVER_NAME     "psensor"

#ifdef CONFIG_HAS_EARLYSUSPEND
static void func_psensor_late_resume(struct early_suspend *);
static void func_psensor_early_suspend(struct early_suspend *);
#endif

struct psensor_data {
	struct switch_dev sdev;
	unsigned gpio;
	unsigned irq;
	struct work_struct work;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif

};

enum Version
{
	WIFISKU = 0,
	VANGOGH3GSKU,
	NOWIFIOR3G,
	NOTPVT,
	PVT_AFTER,
};

static enum Version Boardversion;
extern u16 BoardID(void);
extern unsigned long g_sku_id;

void Checkversion(void)
{
	u16 boardid;

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
	if(g_sku_id == 3)   //3G SKU
	{
		printk( KERN_ERR "%s: this is 3G SKU\n",DRIVER_NAME);
		boardid = BoardID();
		if(boardid == 0xfefe)
		{
			Boardversion = NOTPVT;
			printk( KERN_ERR "%s: EC firmware is not supported, now set NOTPVT\n",
			      DRIVER_NAME);
		} else {
			if(boardid == 2)
			{
				printk( KERN_ERR "%s: this is not after PVT\n",DRIVER_NAME);
				Boardversion = NOTPVT;
			} else if (boardid > 2) {
				Boardversion = PVT_AFTER;
			}
		}
	} else {
		if(g_sku_id == 5)
		{
			printk( KERN_ERR "%s: this is WIFI SKU\n",DRIVER_NAME);
			Boardversion = WIFISKU;
		} else {
			printk( KERN_ERR "%s: this is NOTPVT\n",DRIVER_NAME);
			Boardversion = NOTPVT;
		}
	}
#endif

#if defined CONFIG_MACH_ACER_VANGOGH
	if (g_sku_id == 1) { //3G SKU
		pr_info("%s: this is 3G SKU\n",DRIVER_NAME);
		Boardversion = VANGOGH3GSKU;
	} else {
		if(g_sku_id == 0){ //WIFI SKU
			printk( KERN_ERR "%s: this is WIFI SKU\n",DRIVER_NAME);
			Boardversion = WIFISKU;
		} else {
			printk( KERN_ERR "%s: this is not WIFI OR 3G SKU\n",DRIVER_NAME);
			Boardversion = NOWIFIOR3G;
		}
	}
#endif

}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void func_psensor_early_suspend(struct early_suspend *es)
{

	struct psensor_data *psensor = container_of(es, struct psensor_data, early_suspend);
	printk("%s: early suspend\n", DRIVER_NAME);
	disable_irq(psensor->irq);
	cancel_work_sync(&psensor->work);

}

static void func_psensor_late_resume(struct early_suspend *es)
{
	struct psensor_data *psensor = container_of(es, struct psensor_data, early_suspend);
	int psensor_Status = -1;
	printk("%s: late resume\n", DRIVER_NAME);
	enable_irq(psensor->irq);
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

static void psensor_work(struct work_struct *work)
{
	struct psensor_data *psensor =
	    container_of(work, struct psensor_data, work);

	int psensor_Status = -1;

	psensor_Status = gpio_get_value(psensor->gpio);

#if defined CONFIG_MACH_ACER_VANGOGH
	if (psensor_Status == 1)
		psensor_Status = 0;
	else
		psensor_Status = 1;
#endif
	if(!psensor_Status)
		printk("%s: Nothing approach\n", DRIVER_NAME);
	else
		printk("%s: Something approaches\n", DRIVER_NAME);

	switch_set_state(&psensor->sdev,psensor_Status);
}

static irqreturn_t psensor_interrupt(int irq, void *dev_id)
{
	struct psensor_data *psensor = (struct psensor_data *)dev_id;

	schedule_work(&psensor->work);

	return IRQ_HANDLED;
}

static int psensor_probe(struct platform_device *pdev)
{
	struct gpio_switch_platform_data *pdata = pdev->dev.platform_data;
	struct psensor_data *psensor;
	int ret = -EBUSY;

	if (!pdata)
		return -EBUSY;

	Checkversion();

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
	if(Boardversion != PVT_AFTER)
	{
		return -1;
	}
#endif

#if defined CONFIG_MACH_ACER_VANGOGH
	if(Boardversion != VANGOGH3GSKU)
	{
		return -1;
	}
#endif


	psensor = kzalloc(sizeof(struct psensor_data), GFP_KERNEL);
	if (!psensor)
		return -ENOMEM;

	psensor->gpio = pdata->gpio;
	psensor->irq = gpio_to_irq(pdata->gpio);
	psensor->sdev.print_state = switch_print_state;
	psensor->sdev.name = DRIVER_NAME;
	psensor->sdev.print_name = switch_print_name;
	ret = switch_dev_register(&psensor->sdev);
	if (ret < 0)
		goto err_register_switch;

	ret = gpio_request(psensor->gpio, pdev->name);
	if (ret < 0)
		goto err_request_gpio;

	ret = gpio_direction_input(psensor->gpio);
	if (ret < 0)
		goto err_set_gpio_input;

	INIT_WORK(&psensor->work, psensor_work);

	psensor->irq = gpio_to_irq(psensor->gpio);
	if (psensor->irq < 0) {
		ret = psensor->irq;
		goto err_detect_irq_num_failed;
	}

	ret = request_irq(psensor->irq, psensor_interrupt, IRQF_DISABLED | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		 DRIVER_NAME, psensor);
	if(ret)
	{
		printk("psensor request irq failed\n");
		goto err_request_irq;
	}

	// set current status
	psensor_work(&psensor->work);

#ifdef CONFIG_HAS_EARLYSUSPEND
	psensor->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	psensor->early_suspend.suspend = func_psensor_early_suspend;
	psensor->early_suspend.resume = func_psensor_late_resume;
	register_early_suspend(&psensor->early_suspend);
#endif

	printk(KERN_INFO "psensor probe success\n");

	return 0;

err_request_irq:
	err_detect_irq_num_failed:
err_set_gpio_input:
	gpio_free(psensor->gpio);
err_request_gpio:
	switch_dev_unregister(&psensor->sdev);
err_register_switch:
	kfree(psensor);
	return ret;
}

static int __devexit psensor_remove(struct platform_device *pdev)
{
	struct psensor_data *psensor = platform_get_drvdata(pdev);

	cancel_work_sync(&psensor->work);
	gpio_free(psensor->gpio);
	switch_dev_unregister(&psensor->sdev);
	kfree(psensor);

	return 0;
}

#ifdef CONFIG_PM
static int psensor_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int psensor_resume(struct platform_device *pdev)
{
	return 0;
}
#endif

static struct platform_driver psensor_driver = {
	.probe      = psensor_probe,
	.remove     = __devexit_p(psensor_remove),
#ifdef CONFIG_PM
	.suspend    = psensor_suspend,
	.resume     = psensor_resume,
#endif
	.driver     = {
	.name   = "psensor",
	.owner  = THIS_MODULE,
	},
};

static int __init psensor_init(void)
{
	int err;
	err = platform_driver_register(&psensor_driver);
	if (err)
		goto err_exit;

	printk(KERN_INFO "psensor initial success\n");

	return 0;

err_exit:
	printk(KERN_INFO "psensor register Failed!\n");
	return err;
}

static void __exit psensor_exit(void)
{
	printk(KERN_INFO "psensor driver unregister\n");
	platform_driver_unregister(&psensor_driver);
}

module_init(psensor_init);
module_exit(psensor_exit);

MODULE_DESCRIPTION("Psensor Driver");
MODULE_LICENSE("GPL");
