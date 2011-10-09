/*
 * sound/soc/tegra/tegra_wired_jack.c
 *
 * Copyright (c) 2011, NVIDIA Corporation.
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

#include <linux/types.h>
#include <linux/gpio.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
#include <linux/notifier.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <mach/audio.h>

#include "tegra_soc.h"

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
#include <sound/wm8903.h>
#include "../codecs/wm8903.h"
#include "tegra_wired_jack.h"
#endif

#define HEAD_DET_GPIO 0
#define MIC_DET_GPIO  1
#define SPK_EN_GPIO   3

#if 1
#define ACER_DBG(fmt, arg...) printk(KERN_INFO "[AUDIO]: %s: " fmt "\n", __FUNCTION__, ## arg)
#else
#define ACER_DBG(fmt, arg...) do {} while (0)
#endif

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
int g_mic_state = 0;
struct snd_soc_codec* g_codec;
#endif

extern void MicSwitch_int(void);
extern void MicSwitch_ext(void);

struct wired_jack_conf tegra_wired_jack_conf = {
	-1, -1, -1, -1, 0, NULL, NULL
};

/* Based on hp_gpio and mic_gpio, hp_gpio is active low */
enum {
	HEADSET_WITHOUT_MIC = 0x00,
	HEADSET_WITH_MIC = 0x01,
	NO_DEVICE = 0x10,
	MIC = 0x11,
};

/* These values are copied from WiredAccessoryObserver */
enum headset_state {
	BIT_NO_HEADSET = 0,
	BIT_HEADSET = (1 << 0),
	BIT_HEADSET_NO_MIC = (1 << 1),
};

/* jack */
static struct snd_soc_jack *tegra_wired_jack;

static struct snd_soc_jack_gpio wired_jack_gpios[] = {
	{
		/* gpio pin depends on board traits */
		.name = "headphone-detect-gpio",
		.report = SND_JACK_HEADPHONE,
		.invert = 1,
		.debounce_time = 200,
	},
	{
		/* gpio pin depens on board traits */
		.name = "mic-detect-gpio",
		.report = SND_JACK_MICROPHONE,
		.invert = 1,
		.debounce_time = 200,
	},
};

#ifdef CONFIG_SWITCH
static struct switch_dev wired_switch_dev = {
	.name = "h2w",
};

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
static int wired_jack_detect(void)
{
	int i;
	int withMic;
	int withoutMic;
	int MICDET_EINT_14;
	int MICSHRT_EINT_15;
	int irqStatus;
	int irq_mask = 0x3fff;
	int all_mask = 0xffff;
	int CtrlReg = 0;

	snd_soc_write(g_codec, WM8903_INTERRUPT_STATUS_1_MASK, irq_mask);
	snd_soc_write(g_codec, WM8903_MIC_BIAS_CONTROL_0, WM8903_MICDET_ENA | WM8903_MICBIAS_ENA);

	for(i = 0; i <= 15; i++)
	{
		msleep(1);
		irqStatus = snd_soc_read(g_codec, WM8903_INTERRUPT_STATUS_1);
		MICDET_EINT_14 = (irqStatus >> 14) & 0x1;
		MICSHRT_EINT_15 = (irqStatus >> 15) & 0x1;

		if(MICDET_EINT_14 == MICSHRT_EINT_15)
			withoutMic++;
		else
			withMic++;

		if(i%2 == 0)
			snd_soc_write(g_codec, WM8903_INTERRUPT_POLARITY_1, irq_mask);
		else
			snd_soc_write(g_codec, WM8903_INTERRUPT_POLARITY_1, all_mask);
	}

	CtrlReg &= ~(WM8903_MICDET_ENA | WM8903_MICBIAS_ENA);
	snd_soc_write(g_codec, WM8903_MIC_BIAS_CONTROL_0, CtrlReg);

	if (withMic > withoutMic)
		return 1;
	else
		return 2;
}

void select_mic_input(int state)
{
	int CtrlReg = 0;
	struct wm8903_priv *wm8903_config = (struct wm8903_priv *)snd_soc_codec_get_drvdata(g_codec);

	if (wm8903_config->fm2018_status == 1) {
		CtrlReg |= WM8903_MICDET_ENA | WM8903_MICBIAS_ENA;
		snd_soc_write(g_codec, WM8903_MIC_BIAS_CONTROL_0, CtrlReg);
		ACER_DBG("Enable mic bias !!\n");
	} else if (wm8903_config->fm2018_status == 0) {
		CtrlReg &= ~(WM8903_MICDET_ENA | WM8903_MICBIAS_ENA);
		snd_soc_write(g_codec, WM8903_MIC_BIAS_CONTROL_0, CtrlReg);
		ACER_DBG("Disable mic bias !!\n");
	}

	switch (state) {
		case 0:
		case 2:
		{
#ifdef CONFIG_MACH_ACER_VANGOGH
			MicSwitch_int();
#else
			CtrlReg = (0x0<<B06_IN_CM_ENA) |
				(0x0<<B04_IP_SEL_N) | (0x1<<B02_IP_SEL_P) | (0x0<<B00_MODE);
			snd_soc_write(g_codec, WM8903_ANALOGUE_LEFT_INPUT_1, CtrlReg);
			snd_soc_write(g_codec, WM8903_ANALOGUE_RIGHT_INPUT_1, CtrlReg);
#endif
		}
		break;

		case 1:
		{
#ifdef CONFIG_MACH_ACER_VANGOGH
			MicSwitch_ext();
#else
			CtrlReg = (0x0<<B06_IN_CM_ENA) |
				(0x1<<B04_IP_SEL_N) | (0x1<<B02_IP_SEL_P) | (0x0<<B00_MODE);
			snd_soc_write(g_codec, WM8903_ANALOGUE_LEFT_INPUT_1, CtrlReg);
			snd_soc_write(g_codec, WM8903_ANALOGUE_RIGHT_INPUT_1, CtrlReg);
#endif
		}
		break;
	}
}

int wired_jack_state(void)
{
	return g_mic_state;
}
#endif

void tegra_switch_set_state(int state)
{
	switch_set_state(&wired_switch_dev, state);
}

static enum headset_state get_headset_state(void)
{
	enum headset_state state = BIT_NO_HEADSET;
	int flag = 0;
	int hp_gpio = -1;
	int mic_gpio = -1;

	/* hp_det_n is low active pin */
	if (tegra_wired_jack_conf.hp_det_n != -1)
		hp_gpio = gpio_get_value(tegra_wired_jack_conf.hp_det_n);
#ifndef MACH_ACER_AUDIO
	if (tegra_wired_jack_conf.cdc_irq != -1)
		mic_gpio = gpio_get_value(tegra_wired_jack_conf.cdc_irq);
#endif

	pr_debug("hp_gpio:%d, mic_gpio:%d\n", hp_gpio, mic_gpio);

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
	if (wired_jack_detect() == 1)
		mic_gpio = 1;
	else
		mic_gpio = 0;
#endif
	flag = (hp_gpio << 4) | mic_gpio;

	switch (flag) {
	case NO_DEVICE:
#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
		select_mic_input(2);
#endif
		state = BIT_NO_HEADSET;
		ACER_DBG("NO_DEVICE !\n");
		break;
	case HEADSET_WITH_MIC:
		state = BIT_HEADSET;
#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
		select_mic_input(1);
		ACER_DBG("HEADSET_WITH_MIC !\n");
#endif
		break;
	case MIC:
		/* mic: would not report */
		break;
	case HEADSET_WITHOUT_MIC:
		state = BIT_HEADSET_NO_MIC;
#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
		select_mic_input(2);
		ACER_DBG("HEADSET_WITHOUT_MIC !\n");
#endif
		break;
	default:
		state = BIT_NO_HEADSET;
#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
		select_mic_input(2);
#endif
		ACER_DBG("Default NO_DEVICE !\n");
	}
#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
	g_mic_state = state;
#endif

	return state;
}

static int wired_switch_notify(struct notifier_block *self,
			      unsigned long action, void* dev)
{
	tegra_switch_set_state(get_headset_state());

	return NOTIFY_OK;
}

void tegra_jack_resume(void)
{
	snd_soc_jack_add_gpios(tegra_wired_jack,
				ARRAY_SIZE(wired_jack_gpios),
				wired_jack_gpios);
}

void tegra_jack_suspend(void)
{
	snd_soc_jack_free_gpios(tegra_wired_jack,
				ARRAY_SIZE(wired_jack_gpios),
				wired_jack_gpios);
}

static struct notifier_block wired_switch_nb = {
	.notifier_call = wired_switch_notify,
};
#endif

/* platform driver */
static int tegra_wired_jack_probe(struct platform_device *pdev)
{
	int ret;
#ifdef MACH_ACER_AUDIO
	int hp_det_n;
#else
	int hp_det_n, cdc_irq;
	int en_mic_int, en_mic_ext;
	int en_spkr;
#endif
	struct tegra_wired_jack_conf *pdata;

	pdata = (struct tegra_wired_jack_conf *)pdev->dev.platform_data;
#ifdef MACH_ACER_AUDIO
	if (!pdata || !pdata->hp_det_n) {
#else
	if (!pdata || !pdata->hp_det_n || !pdata->en_spkr ||
	    !pdata->cdc_irq || !pdata->en_mic_int || !pdata->en_mic_ext) {
#endif
		pr_err("Please set up gpio pins for jack.\n");
		return -EBUSY;
	}

	hp_det_n = pdata->hp_det_n;
	wired_jack_gpios[HEAD_DET_GPIO].gpio = hp_det_n;

#ifndef MACH_ACER_AUDIO
	cdc_irq = pdata->cdc_irq;
	wired_jack_gpios[MIC_DET_GPIO].gpio = cdc_irq;
#endif

	ret = snd_soc_jack_add_gpios(tegra_wired_jack,
				     ARRAY_SIZE(wired_jack_gpios),
				     wired_jack_gpios);
	if (ret) {
		pr_err("Could NOT set up gpio pins for jack.\n");
		snd_soc_jack_free_gpios(tegra_wired_jack,
					ARRAY_SIZE(wired_jack_gpios),
					wired_jack_gpios);
		return ret;
	}

#ifndef MACH_ACER_AUDIO
	/* Mic switch controlling pins */
	en_mic_int = pdata->en_mic_int;
	en_mic_ext = pdata->en_mic_ext;

	ret = gpio_request(en_mic_int, "en_mic_int");
	if (ret) {
		pr_err("Could NOT get gpio for internal mic controlling.\n");
		gpio_free(en_mic_int);
	}
	gpio_direction_output(en_mic_int, 0);
	gpio_export(en_mic_int, false);

	ret = gpio_request(en_mic_ext, "en_mic_ext");
	if (ret) {
		pr_err("Could NOT get gpio for external mic controlling.\n");
		gpio_free(en_mic_ext);
	}
	gpio_direction_output(en_mic_ext, 0);
	gpio_export(en_mic_ext, false);

	en_spkr = pdata->en_spkr;
	ret = gpio_request(en_spkr, "en_spkr");
	if (ret) {
		pr_err("Could NOT set up gpio pin for amplifier.\n");
		gpio_free(en_spkr);
	}
	gpio_direction_output(en_spkr, 0);
	gpio_export(en_spkr, false);

	if (pdata->spkr_amp_reg)
		tegra_wired_jack_conf.amp_reg =
			regulator_get(NULL, pdata->spkr_amp_reg);
#endif
	tegra_wired_jack_conf.amp_reg_enabled = 0;

	/* restore configuration of these pins */
	tegra_wired_jack_conf.hp_det_n = hp_det_n;
#ifndef MACH_ACER_AUDIO
	tegra_wired_jack_conf.en_mic_int = en_mic_int;
	tegra_wired_jack_conf.en_mic_ext = en_mic_ext;
	tegra_wired_jack_conf.cdc_irq = cdc_irq;
	tegra_wired_jack_conf.en_spkr = en_spkr;
#endif

	// Communicate the jack connection state at device bootup
	tegra_switch_set_state(get_headset_state());

#ifdef CONFIG_SWITCH
	snd_soc_jack_notifier_register(tegra_wired_jack,
				       &wired_switch_nb);
#endif
	return ret;
}

static int tegra_wired_jack_remove(struct platform_device *pdev)
{
	snd_soc_jack_free_gpios(tegra_wired_jack,
				ARRAY_SIZE(wired_jack_gpios),
				wired_jack_gpios);

#ifndef MACH_ACER_AUDIO
	gpio_free(tegra_wired_jack_conf.en_mic_int);
	gpio_free(tegra_wired_jack_conf.en_mic_ext);
	gpio_free(tegra_wired_jack_conf.en_spkr);
#endif

	if (tegra_wired_jack_conf.amp_reg) {
		if (tegra_wired_jack_conf.amp_reg_enabled)
			regulator_disable(tegra_wired_jack_conf.amp_reg);
		regulator_put(tegra_wired_jack_conf.amp_reg);
	}

	return 0;
}

static struct platform_driver tegra_wired_jack_driver = {
	.probe = tegra_wired_jack_probe,
	.remove = tegra_wired_jack_remove,
	.driver = {
		.name = "tegra_wired_jack",
		.owner = THIS_MODULE,
	},
};


int tegra_jack_init(struct snd_soc_codec *codec)
{
	int ret;

	if (!codec)
		return -1;

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
	g_codec = codec;
#endif

	tegra_wired_jack = kzalloc(sizeof(*tegra_wired_jack), GFP_KERNEL);
	if (!tegra_wired_jack) {
		pr_err("failed to allocate tegra_wired_jack \n");
		return -ENOMEM;
	}

	/* Add jack detection */
	ret = snd_soc_jack_new(codec->socdev->card, "Wired Accessory Jack",
			       SND_JACK_HEADSET, tegra_wired_jack);
	if (ret < 0)
		goto failed;

#ifdef CONFIG_SWITCH
	/* Addd h2w swith class support */
	ret = switch_dev_register(&wired_switch_dev);
	if (ret < 0)
		goto switch_dev_failed;
#endif

	ret = platform_driver_register(&tegra_wired_jack_driver);
	if (ret < 0)
		goto platform_dev_failed;

	return 0;

#ifdef CONFIG_SWITCH
switch_dev_failed:
	switch_dev_unregister(&wired_switch_dev);
#endif
platform_dev_failed:
	platform_driver_unregister(&tegra_wired_jack_driver);
failed:
	if (tegra_wired_jack) {
		kfree(tegra_wired_jack);
		tegra_wired_jack = 0;
	}
	return ret;
}

void tegra_jack_exit(void)
{
#ifdef CONFIG_SWITCH
	switch_dev_unregister(&wired_switch_dev);
#endif
	platform_driver_unregister(&tegra_wired_jack_driver);

	if (tegra_wired_jack) {
		kfree(tegra_wired_jack);
		tegra_wired_jack = 0;
	}
}
