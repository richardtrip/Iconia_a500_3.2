/*
 * arch/arm/mach-tegra/board-ventana.c
 *
 * Copyright (c) 2010 - 2011, NVIDIA Corporation.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/usb/android_composite.h>
#include <linux/usb/f_accessory.h>
#include <linux/mfd/tps6586x.h>
#include <linux/memblock.h>

#ifdef CONFIG_TOUCHSCREEN_PANJIT_I2C
#include <linux/i2c/panjit_ts.h>
#endif

#include <sound/wm8903.h>

#include <mach/clk.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <mach/i2s.h>
#include <mach/spdif.h>
#include <mach/audio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/usb_phy.h>
#include <mach/tegra_das.h>

#include "board.h"
#include "clock.h"
#include "board-ventana.h"
#include "devices.h"
#include "gpio-names.h"
#include "fuse.h"
#include "wakeups-t2.h"

#ifdef CONFIG_ANDROID_TIMED_GPIO
#include <../../../drivers/staging/android/timed_output.h>
#include <../../../drivers/staging/android/timed_gpio.h>
#endif

#ifdef CONFIG_MACH_ACER_VANGOGH
extern void empty_gpio_init(void);
#endif

extern void SysShutdown(void);

unsigned long g_sku_id;

extern unsigned int total_ram_size ;
extern char acer_brand[20];

static struct usb_mass_storage_platform_data tegra_usb_fsg_platform = {
	.vendor = "NVIDIA",
	.product = "Tegra 2",
	.nluns = 1,
};

static struct platform_device tegra_usb_fsg_device = {
	.name = "usb_mass_storage",
	.id = -1,
	.dev = {
		.platform_data = &tegra_usb_fsg_platform,
	},
};

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTD_BASE),
		.mapbase	= TEGRA_UARTD_BASE,
		.irq		= INT_UARTD,
		.flags		= UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE,
		.type           = PORT_TEGRA,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0,
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

static struct tegra_audio_platform_data tegra_spdif_pdata = {
	.dma_on = true,  /* use dma by default */
	.spdif_clk_rate = 5644800,
};

static struct tegra_utmip_config utmi_phy_config[] = {
	[0] = {
			.hssync_start_delay = 9,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 15,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
	[1] = {
			.hssync_start_delay = 9,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 8,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
};

static struct tegra_ulpi_config ulpi_phy_config = {
	.reset_gpio = TEGRA_GPIO_PG2,
	.clk = "clk_dev2",
	.inf_type = TEGRA_USB_LINK_ULPI,
};

#ifdef CONFIG_BCM4329_RFKILL

static struct resource ventana_bcm4329_rfkill_resources[] = {
	{
		.name   = "bcm4329_nshutdown_gpio",
		.start  = TEGRA_GPIO_PU0,
		.end    = TEGRA_GPIO_PU0,
		.flags  = IORESOURCE_IO,
	},
};

static struct platform_device ventana_bcm4329_rfkill_device = {
	.name = "bcm4329_rfkill",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(ventana_bcm4329_rfkill_resources),
	.resource       = ventana_bcm4329_rfkill_resources,
};

static noinline void __init ventana_bt_rfkill(void)
{
	/*Add Clock Resource*/
	clk_add_alias("bcm4329_32k_clk", ventana_bcm4329_rfkill_device.name, \
				"blink", NULL);

	platform_device_register(&ventana_bcm4329_rfkill_device);

	return;
}
#else
static inline void ventana_bt_rfkill(void) { }
#endif

#ifdef CONFIG_BT_BLUESLEEP
static noinline void __init tegra_setup_bluesleep(void)
{
	struct platform_device *pdev = NULL;
	struct resource *res;

	pdev = platform_device_alloc("bluesleep", 0);
	if (!pdev) {
		pr_err("unable to allocate platform device for bluesleep");
		return;
	}

	res = kzalloc(sizeof(struct resource) * 3, GFP_KERNEL);
	if (!res) {
		pr_err("unable to allocate resource for bluesleep\n");
		goto err_free_dev;
	}

	res[0].name   = "gpio_host_wake";
	res[0].start  = TEGRA_GPIO_PU6;
	res[0].end    = TEGRA_GPIO_PU6;
	res[0].flags  = IORESOURCE_IO;

	res[1].name   = "gpio_ext_wake";
	res[1].start  = TEGRA_GPIO_PU1;
	res[1].end    = TEGRA_GPIO_PU1;
	res[1].flags  = IORESOURCE_IO;

	res[2].name   = "host_wake";
	res[2].start  = gpio_to_irq(TEGRA_GPIO_PU6);
	res[2].end    = gpio_to_irq(TEGRA_GPIO_PU6);
	res[2].flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE;

	if (platform_device_add_resources(pdev, res, 3)) {
		pr_err("unable to add resources to bluesleep device\n");
		goto err_free_res;
	}

	if (platform_device_add(pdev)) {
		pr_err("unable to add bluesleep device\n");
		goto err_free_res;
	}

	tegra_gpio_enable(TEGRA_GPIO_PU6);
	tegra_gpio_enable(TEGRA_GPIO_PU1);

	return;

err_free_res:
	kfree(res);
err_free_dev:
	platform_device_put(pdev);
	return;
}
#else
static inline void tegra_setup_bluesleep(void) { }
#endif

static __initdata struct tegra_clk_init_table ventana_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartd",	"pll_p",	216000000,	true},
	{ "uartc",	"pll_m",	600000000,	false},
	{ "blink",	"clk_32k",	32768,		false},
	{ "pll_p_out4",	"pll_p",	24000000,	true },
	{ "pwm",	"clk_m",	12000000,	false},
	{ "pll_a",	NULL,		56448000,	false},
	{ "pll_a_out0",	NULL,		11289600,	false},
	{ "clk_dev1",	"pll_a_out0",	0,		true},
	{ "i2s1",	"pll_a_out0",	11289600,	false},
	{ "i2s2",	"pll_a_out0",	11289600,	false},
	{ "audio",	"pll_a_out0",	11289600,	false},
	{ "audio_2x",	"audio",	22579200,	false},
	{ "spdif_out",	"pll_a_out0",	5644800,	false},
	{ "kbc",	"clk_32k",	32768,		true},
	{ "vde",	"pll_m",	240000000,	false},
	{ NULL,		NULL,		0,		0},
};

#define USB_MANUFACTURER_NAME		"ACER"
#define USB_PRODUCT_NAME_PICA_WIFI	"ACER Iconia Tab A500"
#define USB_PRODUCT_NAME_PICA_3G	"ACER Iconia Tab A501"
//TODO: confirm vangogh product name
#define USB_PRODUCT_NAME_VG_WIFI	"ACER Iconia Tab A100"
#define USB_PRODUCT_NAME_VG_3G		"ACER Iconia Tab A101"

#define USB_PID_PICA_WIFI_MTP_ADB	0x3325
#define USB_PID_PICA_WIFI_MTP		0x3341
#define USB_PID_PICA_3G_MTP_ADB	0x3344
#define USB_PID_PICA_3G_MTP		0x3345
#define USB_PID_PICA_3G_RNDIS_ADB	0x3346
#define USB_PID_PICA_3G_RNDIS		0x3347

#define USB_PID_VG_WIFI_MTP_ADB	0x3348
#define USB_PID_VG_WIFI_MTP		0x3349
#define USB_PID_VG_3G_MTP_ADB		0x334A
#define USB_PID_VG_3G_MTP		0x334B
#define USB_PID_VG_3G_RNDIS_ADB	0x334C
#define USB_PID_VG_3G_RNDIS		0x334D

#define USB_PID_PICA_WIFI_ACC_ADB	USB_ACCESSORY_ADB_PRODUCT_ID
#define USB_PID_PICA_WIFI_ACC		USB_ACCESSORY_PRODUCT_ID
#define USB_PID_PICA_3G_ACC_ADB	USB_ACCESSORY_ADB_PRODUCT_ID
#define USB_PID_PICA_3G_ACC		USB_ACCESSORY_PRODUCT_ID
#define USB_PID_VG_WIFI_ACC_ADB	USB_ACCESSORY_ADB_PRODUCT_ID
#define USB_PID_VG_WIFI_ACC		USB_ACCESSORY_PRODUCT_ID
#define USB_PID_VG_3G_ACC_ADB		USB_ACCESSORY_ADB_PRODUCT_ID
#define USB_PID_VG_3G_ACC		USB_ACCESSORY_PRODUCT_ID

#define USB_VENDOR_ID			0x0502

static char *usb_functions_mtp[] = { "mtp"};
static char *usb_functions_mtp_adb[] = { "mtp", "adb"};
#ifdef CONFIG_USB_ANDROID_RNDIS
static char *usb_functions_rndis[] = { "rndis" };
static char *usb_functions_rndis_adb[] = { "rndis", "adb" };
#endif
#ifdef CONFIG_USB_ANDROID_ACCESSORY
static char *usb_functions_accessory[] = { "accessory"};
static char *usb_functions_accessory_adb[] = { "accessory", "adb"};
#endif
static char *usb_functions_all[] = {
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
#ifdef CONFIG_USB_ANDROID_ACCESSORY
	"accessory",
#endif
#ifdef CONFIG_USB_ANDROID_MTP
	"mtp",
#endif
	"adb"
};

static struct android_usb_product usb_products[] = {
	{
		.product_id     = USB_PID_PICA_WIFI_MTP,
		.num_functions  = ARRAY_SIZE(usb_functions_mtp),
		.functions      = usb_functions_mtp,
	},
	{
		.product_id     = USB_PID_PICA_WIFI_MTP_ADB,
		.num_functions  = ARRAY_SIZE(usb_functions_mtp_adb),
		.functions      = usb_functions_mtp_adb,
	},
#ifdef CONFIG_USB_ANDROID_RNDIS
	{
		.product_id     = USB_PID_PICA_3G_RNDIS,
		.num_functions  = ARRAY_SIZE(usb_functions_rndis),
		.functions      = usb_functions_rndis,
	},
	{
		.product_id     = USB_PID_PICA_3G_RNDIS_ADB,
		.num_functions  = ARRAY_SIZE(usb_functions_rndis_adb),
		.functions      = usb_functions_rndis_adb,
	},
#endif
#ifdef CONFIG_USB_ANDROID_ACCESSORY
	{
		.vendor_id      = USB_ACCESSORY_VENDOR_ID,
		.product_id     = USB_PID_PICA_WIFI_ACC,
		.num_functions  = ARRAY_SIZE(usb_functions_accessory),
		.functions      = usb_functions_accessory,
	},
	{
		.vendor_id      = USB_ACCESSORY_VENDOR_ID,
		.product_id     = USB_PID_PICA_WIFI_ACC_ADB,
		.num_functions  = ARRAY_SIZE(usb_functions_accessory_adb),
		.functions      = usb_functions_accessory_adb,
	},
#endif
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id              = USB_VENDOR_ID,
	.product_id             = USB_PID_PICA_WIFI_MTP_ADB,
	.manufacturer_name      = USB_MANUFACTURER_NAME,
	.product_name           = USB_PRODUCT_NAME_PICA_WIFI,
	.serial_number          = NULL,
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_all),
	.functions = usb_functions_all,
};

static struct platform_device androidusb_device = {
	.name   = "android_usb",
	.id     = -1,
	.dev    = {
		.platform_data  = &andusb_plat,
	},
};

#ifdef CONFIG_USB_ANDROID_RNDIS
static struct usb_ether_platform_data rndis_pdata = {
	.ethaddr = {0, 0, 0, 0, 0, 0},
	.vendorID = USB_VENDOR_ID,
	.vendorDescr = USB_MANUFACTURER_NAME,
};

static struct platform_device rndis_device = {
	.name   = "rndis",
	.id     = -1,
	.dev    = {
		.platform_data  = &rndis_pdata,
	},
};
#endif

static struct wm8903_platform_data wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0x83,           /* enable mic bias current */
	.micdet_delay = 0,
	.gpio_base = WM8903_GPIO_BASE,
	.gpio_cfg = {
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
		0,                     /* as output pin */
		WM8903_GPn_FN_GPIO_MICBIAS_CURRENT_DETECT
		<< WM8903_GP4_FN_SHIFT, /* as micbias current detect */
		WM8903_GPIO_NO_CONFIG,
	},
};

static struct i2c_board_info __initdata ventana_i2c_bus1_board_info[] = {
	{
		I2C_BOARD_INFO("wm8903", 0x1a),
		.platform_data = &wm8903_pdata,
	},
};

static struct tegra_ulpi_config ventana_ehci2_ulpi_phy_config = {
	.reset_gpio = TEGRA_GPIO_PV1,
	.clk = "clk_dev2",
};

static struct tegra_ehci_platform_data ventana_ehci2_ulpi_platform_data = {
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 1,
	.phy_config = &ventana_ehci2_ulpi_phy_config,
};

static struct tegra_i2c_platform_data ventana_i2c1_platform_data = {
	.adapter_nr	= 0,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.slave_addr = 0x00FC,
};

static const struct tegra_pingroup_config i2c2_ddc = {
	.pingroup	= TEGRA_PINGROUP_DDC,
	.func		= TEGRA_MUX_I2C2,
};

static const struct tegra_pingroup_config i2c2_gen2 = {
	.pingroup	= TEGRA_PINGROUP_PTA,
	.func		= TEGRA_MUX_I2C2,
};

static struct tegra_i2c_platform_data ventana_i2c2_platform_data = {
	.adapter_nr	= 1,
	.bus_count	= 2,
	.bus_clk_rate	= { 50000, 100000 },
	.bus_mux	= { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len	= { 1, 1 },
	.slave_addr = 0x00FC,
};

static struct tegra_i2c_platform_data ventana_i2c3_platform_data = {
	.adapter_nr	= 3,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.slave_addr = 0x00FC,
};

static struct tegra_i2c_platform_data ventana_dvc_platform_data = {
	.adapter_nr	= 4,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.is_dvc		= true,
};

static struct tegra_audio_platform_data tegra_audio_pdata[] = {
	/* For I2S1 */
	[0] = {
		.i2s_master	= true,
		.dma_on		= true,  /* use dma by default */
		.i2s_master_clk = 44100,
		.i2s_clk_rate	= 11289600,
		.dap_clk	= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode		= I2S_BIT_FORMAT_I2S,
		.fifo_fmt	= I2S_FIFO_PACKED,
		.bit_size	= I2S_BIT_SIZE_16,
		.i2s_bus_width = 32,
		.dsp_bus_width = 16,
		.en_dmic = false, /* by default analog mic is used */
	},
	/* For I2S2 */
	[1] = {
		.i2s_master	= true,
		.dma_on		= true,  /* use dma by default */
		.i2s_master_clk = 8000,
		.dsp_master_clk = 8000,
		.i2s_clk_rate	= 2000000,
		.dap_clk	= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode		= I2S_BIT_FORMAT_DSP,
		.fifo_fmt	= I2S_FIFO_16_LSB,
		.bit_size	= I2S_BIT_SIZE_16,
		.i2s_bus_width = 32,
		.dsp_bus_width = 16,
	}
};

static struct tegra_das_platform_data tegra_das_pdata = {
	.dap_clk = "clk_dev1",
	.tegra_dap_port_info_table = {
		/* I2S1 <--> DAC1 <--> DAP1 <--> Hifi Codec */
		[0] = {
			.dac_port = tegra_das_port_i2s1,
			.dap_port = tegra_das_port_dap1,
			.codec_type = tegra_audio_codec_type_hifi,
			.device_property = {
				.num_channels = 2,
				.bits_per_sample = 16,
				.rate = 44100,
				.dac_dap_data_comm_format =
						dac_dap_data_format_all,
			},
		},
		[1] = {
			.dac_port = tegra_das_port_none,
			.dap_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		[2] = {
			.dac_port = tegra_das_port_none,
			.dap_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		/* I2S2 <--> DAC2 <--> DAP4 <--> BT SCO Codec */
		[3] = {
			.dac_port = tegra_das_port_i2s2,
			.dap_port = tegra_das_port_dap4,
			.codec_type = tegra_audio_codec_type_bluetooth,
			.device_property = {
				.num_channels = 1,
				.bits_per_sample = 16,
				.rate = 8000,
				.dac_dap_data_comm_format =
					dac_dap_data_format_dsp,
			},
		},
		[4] = {
			.dac_port = tegra_das_port_none,
			.dap_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
	},

	.tegra_das_con_table = {
		[0] = {
			.con_id = tegra_das_port_con_id_hifi,
			.num_entries = 2,
			.con_line = {
				[0] = {tegra_das_port_i2s1, tegra_das_port_dap1, true},
				[1] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
			},
		},
		[1] = {
			.con_id = tegra_das_port_con_id_bt_codec,
			.num_entries = 4,
			.con_line = {
				[0] = {tegra_das_port_i2s2, tegra_das_port_dap4, true},
				[1] = {tegra_das_port_dap4, tegra_das_port_i2s2, false},
				[2] = {tegra_das_port_i2s1, tegra_das_port_dap1, true},
				[3] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
			},
		},
	}
};

static void ventana_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &ventana_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &ventana_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &ventana_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &ventana_dvc_platform_data;

	i2c_register_board_info(0, ventana_i2c_bus1_board_info, 1);

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
}


#ifdef CONFIG_KEYBOARD_GPIO
#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_VANGOGH) || \
	defined(CONFIG_MACH_ACER_MAYA)
#define GPIO_KEY(_id, _gpio, _isactivelow, _iswake)		\
	{					\
		.code = _id,			\
		.gpio = TEGRA_GPIO_##_gpio,	\
		.active_low = _isactivelow,	\
		.desc = #_id,			\
		.type = EV_KEY,			\
		.wakeup = _iswake,		\
		.debounce_interval = 10,	\
	}
#else
#define GPIO_KEY(_id, _gpio, _iswake)		\
	{					\
		.code = _id,			\
		.gpio = TEGRA_GPIO_##_gpio,	\
		.active_low = 1,		\
		.desc = #_id,			\
		.type = EV_KEY,			\
		.wakeup = _iswake,		\
		.debounce_interval = 10,	\
	}
#endif
#ifdef CONFIG_MACH_ACER_VANGOGH
static struct gpio_keys_button ventana_keys[] = {
	[0] = GPIO_KEY(KEY_VOLUMEUP, PQ4, 1,  0),
	[1] = GPIO_KEY(KEY_VOLUMEDOWN, PQ5, 1, 0),
	[2] = GPIO_KEY(KEY_POWER, PC7, 0, 1),
	[3] = GPIO_KEY(KEY_POWER, PI3, 0, 0),
	[4] = GPIO_KEY(KEY_HOME, PQ1, 0, 0),
};
#elif defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)

static struct gpio_keys_button ventana_keys[] = {
	[0] = GPIO_KEY(KEY_VOLUMEUP, PQ4, 1,  0),
	[1] = GPIO_KEY(KEY_VOLUMEDOWN, PQ5, 1, 0),
	[2] = GPIO_KEY(KEY_POWER, PC7, 0, 1),
	[3] = GPIO_KEY(KEY_POWER, PI3, 0, 0),
};
#else
static struct gpio_keys_button ventana_keys[] = {
	[0] = GPIO_KEY(KEY_FIND, PQ3, 0),
	[1] = GPIO_KEY(KEY_HOME, PQ1, 0),
	[2] = GPIO_KEY(KEY_BACK, PQ2, 0),
	[3] = GPIO_KEY(KEY_VOLUMEUP, PQ5, 0),
	[4] = GPIO_KEY(KEY_VOLUMEDOWN, PQ4, 0),
	[5] = GPIO_KEY(KEY_POWER, PV2, 1),
	[6] = GPIO_KEY(KEY_MENU, PC7, 0),
};
#endif

#define PMC_WAKE_STATUS 0x14

static int ventana_wakeup_key(void)
{
	unsigned long status =
		readl(IO_ADDRESS(TEGRA_PMC_BASE) + PMC_WAKE_STATUS);

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || \
    defined(CONFIG_MACH_ACER_VANGOGH)
	return status & TEGRA_WAKE_GPIO_PC7 ? KEY_POWER : KEY_RESERVED;
#else
	return status & TEGRA_WAKE_GPIO_PV2 ? KEY_POWER : KEY_RESERVED;
#endif
}

static struct gpio_keys_platform_data ventana_keys_platform_data = {
	.buttons	= ventana_keys,
	.nbuttons	= ARRAY_SIZE(ventana_keys),
	.wakeup_key	= ventana_wakeup_key,
};

static struct platform_device ventana_keys_device = {
	.name	= "gpio-keys",
	.id	= 0,
	.dev	= {
		.platform_data	= &ventana_keys_platform_data,
	},
};

static void ventana_keys_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ventana_keys); i++)
		tegra_gpio_enable(ventana_keys[i].gpio);
}
#endif

#ifdef CONFIG_DOCK
static struct gpio_switch_platform_data dock_switch_platform_data = {
	.gpio = TEGRA_GPIO_PR0,
};

static struct platform_device dock_switch = {
	.name   = "acer-dock",
	.id     = -1,
	.dev    = {
		.platform_data  = &dock_switch_platform_data,
	},
};
#endif

#ifdef CONFIG_ROTATELOCK
static struct gpio_switch_platform_data rotationlock_switch_platform_data = {
	.gpio = TEGRA_GPIO_PQ2,
};

static struct platform_device rotationlock_switch = {
	.name   = "rotationlock",
	.id     = -1,
	.dev    = {
		.platform_data  = &rotationlock_switch_platform_data,
	},
};
#endif

#ifdef CONFIG_PSENSOR
static struct gpio_switch_platform_data psensor_switch_platform_data = {
	.gpio = TEGRA_GPIO_PC1,
};

static struct platform_device psensor_switch = {
	.name   = "psensor",
	.id     = -1,
	.dev    = {
		.platform_data  = &psensor_switch_platform_data,
	},
};
#endif

#ifdef CONFIG_SIMDETECT
static struct gpio_switch_platform_data simdetect_switch_platform_data = {
        .gpio = TEGRA_GPIO_PI7,
};

static struct platform_device picasso_simdetect_switch = {
        .name   = "simdetect",
        .id     = -1,
        .dev    = {
                .platform_data  = &simdetect_switch_platform_data,
        },
};
#endif

#ifdef CONFIG_ANDROID_TIMED_GPIO
static struct timed_gpio picasso_timed_gpios[] = {
        {
                .name = "vibrator",
                .gpio = TEGRA_GPIO_PV5,
                .max_timeout = 10000,
                .active_low = 0,
        },
};

static struct timed_gpio_platform_data picasso_timed_gpio_platform_data = {
        .num_gpios      = ARRAY_SIZE(picasso_timed_gpios),
        .gpios          = picasso_timed_gpios,
};

static struct platform_device picasso_timed_gpio_device = {
        .name   = TIMED_GPIO_NAME,
        .id     = 0,
        .dev    = {
                .platform_data  = &picasso_timed_gpio_platform_data,
        },
};
#endif

static struct platform_device tegra_camera = {
	.name = "tegra_camera",
	.id = -1,
};

#ifdef CONFIG_ANDROID_RAM_CONSOLE
struct resource tegra_ram_console_resources[] = {
	[0] = {
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_MEM,
	}
};

struct platform_device tegra_ram_console_device = {
	.name			= "ram_console",
	.id 			= -1,
	.num_resources	= ARRAY_SIZE(tegra_ram_console_resources),
	.resource		= tegra_ram_console_resources,
};
#endif

static struct platform_device *ventana_devices[] __initdata = {
	&tegra_usb_fsg_device,
	&androidusb_device,
	&tegra_uartb_device,
	&tegra_uartc_device,
	&pmu_device,
	&tegra_udc_device,
	&tegra_gart_device,
	&tegra_aes_device,
#ifdef CONFIG_KEYBOARD_GPIO
	&ventana_keys_device,
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
        &picasso_timed_gpio_device,
#endif
	&tegra_wdt_device,
	&tegra_i2s_device1,
	&tegra_i2s_device2,
#ifdef CONFIG_ROTATELOCK
	&rotationlock_switch,
#endif
#ifdef CONFIG_PSENSOR
	&psensor_switch,
#endif
	&tegra_spdif_device,
	&tegra_avp_device,
	&tegra_camera,
	&tegra_das_device,
#ifdef CONFIG_ANDROID_RAM_CONSOLE
	&tegra_ram_console_device,
#endif
};

#ifdef CONFIG_TOUCHSCREEN_CYPRESS
#include <linux/input/cyttsp.h>
#define CY_I2C_IRQ_GPIO        TEGRA_GPIO_PV6
#define CY_I2C_ADR             CY_TCH_I2C_ADDR  /* LSTS Operational mode I2C address */
#define CY_I2C_VKEY_NAME       "virtualkeys.cyttsp-i2c" /* must match I2C name */
#define CY_MAXX 1024
#define CY_MAXY 600
#define CY_VK_SZ_X             60
#define CY_VK_SZ_Y             80
#define CY_VK_CNTR_X1          (CY_VK_SZ_X*0)+(CY_VK_SZ_X/2)
#define CY_VK_CNTR_X2          (CY_VK_SZ_X*1)+(CY_VK_SZ_X/2)
#define CY_VK_CNTR_X3          (CY_VK_SZ_X*2)+(CY_VK_SZ_X/2)
#define CY_VK_CNTR_X4          (CY_VK_SZ_X*3)+(CY_VK_SZ_X/2)
#define CY_VK_CNTR_Y1          CY_MAXY+(CY_VK_SZ_Y/2)
#define CY_VK_CNTR_Y2          CY_MAXY+(CY_VK_SZ_Y/2)
#define CY_VK_CNTR_Y3          CY_MAXY+(CY_VK_SZ_Y/2)
#define CY_VK_CNTR_Y4          CY_MAXY+(CY_VK_SZ_Y/2)

/* for DVK geometry
#define CY_VK1_POS             ":30:280:60:80"
#define CY_VK2_POS             ":90:280:60:80"
#define CY_VK3_POS             ":150:280:60:80"
#define CY_VK4_POS             ":210:280:60:80"
*/
#define CY_VK1_POS             ":95:770:190:60"
#define CY_VK2_POS             ":285:770:190:60"
#define CY_VK3_POS             ":475:770:190:60"
#define CY_VK4_POS             ":665:770:190:60"

enum cyttsp_gest {
       CY_GEST_GRP_NONE = 0x0F,
       CY_GEST_GRP1 =  0x10,
       CY_GEST_GRP2 = 0x20,
       CY_GEST_GRP3 = 0x40,
       CY_GEST_GRP4 = 0x80,
};

/* default bootloader keys */
u8 dflt_bl_keys[] = {
       0, 1, 2, 3, 4, 5, 6, 7
};

#if 0   //blue--
/*virtual key support */
static ssize_t cyttsp_vkeys_show(struct kobject *kobj,
                        struct kobj_attribute *attr, char *buf)
{
       return sprintf(buf,
       __stringify(EV_KEY) ":" __stringify(KEY_BACK) CY_VK1_POS
       ":" __stringify(EV_KEY) ":" __stringify(KEY_MENU) CY_VK2_POS
       ":" __stringify(EV_KEY) ":" __stringify(KEY_HOME) CY_VK3_POS
       ":" __stringify(EV_KEY) ":" __stringify(KEY_SEARCH) CY_VK4_POS
       "\n");
}

static struct kobj_attribute cyttsp_vkeys_attr = {
        .attr = {
                .mode = S_IRUGO,
        },
        .show = &cyttsp_vkeys_show,
};

static struct attribute *cyttsp_properties_attrs[] = {
        &cyttsp_vkeys_attr.attr,
        NULL
};

static struct attribute_group cyttsp_properties_attr_group = {
        .attrs = cyttsp_properties_attrs,
};

static int cyttsp_vkey_init(const char *name)
{
       struct kobject *properties_kobj;
       int rc;
       char buf[160];

       cyttsp_vkeys_attr.attr.name = name;
       properties_kobj = kobject_create_and_add("board_properties", NULL);
       rc = 0;
       if (properties_kobj)
               rc = sysfs_create_group(properties_kobj,
                       &cyttsp_properties_attr_group);

       if (!properties_kobj)
               pr_err("%s: "
                       "setup cyttsp virtual keys fail nobj \n",
                       __func__);
       if (rc)
               pr_err("%s: "
                       "setup cyttsp virtual keys fail rc=%d \n",
                       __func__, rc);

       if (!properties_kobj || rc)
               pr_err( "%s: failed to create board_properties\n",
               pr_err( "%s: failed to create board_properties\n",
                       __func__);
       else {
               pr_info("%s: "
                       "setup cyttsp virtual keys ok name=%s\n",
                       __func__, cyttsp_vkeys_attr.attr.name);
               cyttsp_vkeys_show(properties_kobj, &cyttsp_vkeys_attr, buf);
               pr_info("%s: " "%s""\n", __func__, buf);
       }
       return rc;
}
#endif

static int cyttsp_i2c_init(void)
{
       int ret;

//     ret = cyttsp_vkey_init(CY_I2C_VKEY_NAME);
       ret = gpio_request(CY_I2C_IRQ_GPIO, "CYTTSP I2C IRQ GPIO");
       if (ret) {
               pr_err("%s: Failed to request GPIO %d\n",
                      __func__, CY_I2C_IRQ_GPIO);
               return ret;
       }
       gpio_direction_input(CY_I2C_IRQ_GPIO);
       return 0;
}

static void cyttsp_i2c_exit(void)
{
       gpio_free(CY_I2C_IRQ_GPIO);
}

static int cyttsp_i2c_wakeup(void)
{
       return 0;
}

static struct cyttsp_platform_data cypress_i2c_ttsp_platform_data = {
       .wakeup = cyttsp_i2c_wakeup,
       .init = cyttsp_i2c_init,
       .exit = cyttsp_i2c_exit,
       .maxx = CY_MAXX,
       .maxy = CY_MAXY,
       .use_hndshk = false,
       .use_sleep = true,
//     .use_virtual_keys = 1,
//     .use_force_fw_update = 0,
       /* activate up to 4 groups
        * and set active distance
        */
       .act_dist = CY_GEST_GRP_NONE & CY_ACT_DIST_DFLT,
       /* change act_intrvl to customize the Active power state
        * scanning/processing refresh interval for Operating mode
        */
       .act_intrvl = CY_ACT_INTRVL_DFLT,
       /* change tch_tmout to customize the touch timeout for the
        * Active power state for Operating mode
        */
       .tch_tmout = CY_TCH_TMOUT_DFLT,
       /* change lp_intrvl to customize the Low Power power state
        * scanning/processing refresh interval for Operating mode
        */
       .lp_intrvl = CY_LP_INTRVL_DFLT,
       .name = CY_I2C_NAME,
       .irq_gpio = CY_I2C_IRQ_GPIO,
       .bl_keys = dflt_bl_keys,
};

static const struct i2c_board_info ventana_i2c_bus1_touch_info[] = {
        {
                I2C_BOARD_INFO(CY_I2C_NAME, CY_I2C_ADR),
                .irq = TEGRA_GPIO_TO_IRQ(CY_I2C_IRQ_GPIO),
                .platform_data = &cypress_i2c_ttsp_platform_data,
        },
};

static int __init ventana_touch_init_cypress(void)
{
       tegra_gpio_enable(TEGRA_GPIO_PV6);
       tegra_gpio_enable(TEGRA_GPIO_PQ7);
       i2c_register_board_info(0, ventana_i2c_bus1_touch_info, 1);
       return 0;
}
#endif



#ifdef CONFIG_TOUCHSCREEN_PANJIT_I2C
static struct panjit_i2c_ts_platform_data panjit_data = {
	.gpio_reset = TEGRA_GPIO_PQ7,
};

static const struct i2c_board_info ventana_i2c_bus1_touch_info[] = {
	{
	 I2C_BOARD_INFO("panjit_touch", 0x3),
	 .irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	 .platform_data = &panjit_data,
	 },
};

static int __init ventana_touch_init_panjit(void)
{
	tegra_gpio_enable(TEGRA_GPIO_PV6);

	tegra_gpio_enable(TEGRA_GPIO_PQ7);
	i2c_register_board_info(0, ventana_i2c_bus1_touch_info, 1);

	return 0;
}
#endif

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MT_T9
/* Atmel MaxTouch touchscreen              Driver data */

static struct i2c_board_info __initdata i2c_info[] = {
	{
		I2C_BOARD_INFO("maXTouch", 0X4C),
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	},
};

static int __init ventana_touch_init_atmel(void)
{
	int ret;

	tegra_gpio_enable(TEGRA_GPIO_PV6);
	tegra_gpio_enable(TEGRA_GPIO_PQ7);

	ret = gpio_request(TEGRA_GPIO_PV6, "atmel_maXTouch1386_irq_gpio");
	if (ret < 0)
		printk("atmel_maXTouch1386: gpio_request TEGRA_GPIO_PQ6 fail\n");

	ret = gpio_request(TEGRA_GPIO_PQ7, "atmel_maXTouch1386");
	if (ret < 0)
		printk("atmel_maXTouch1386: gpio_request fail\n");

	ret = gpio_direction_output(TEGRA_GPIO_PQ7, 0);
	if (ret < 0)
		printk("atmel_maXTouch1386: gpio_direction_output fail\n");
	gpio_set_value(TEGRA_GPIO_PQ7, 0);
	msleep(1);
	gpio_set_value(TEGRA_GPIO_PQ7, 1);
	msleep(100);

	i2c_register_board_info(0, i2c_info, 1);

	return 0;
}
#endif

static struct usb_phy_plat_data tegra_usb_phy_pdata[] = {
	[0] = {
			.instance = 0,
			.vbus_irq = TPS6586X_INT_BASE + TPS6586X_INT_USB_DET,
			.vbus_gpio = TEGRA_GPIO_PD0,
	},
	[1] = {
			.instance = 1,
			.vbus_gpio = -1,
	},
	[2] = {
			.instance = 2,
			.vbus_gpio = TEGRA_GPIO_PD3,
	},
};

static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
			.phy_config = &utmi_phy_config[0],
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
	[1] = {
			.phy_config = &ulpi_phy_config,
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
	[2] = {
			.phy_config = &utmi_phy_config[1],
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
};

static struct platform_device *tegra_usb_otg_host_register(void)
{
	struct platform_device *pdev;
	void *platform_data;
	int val;

	pdev = platform_device_alloc(tegra_ehci1_device.name, tegra_ehci1_device.id);
	if (!pdev)
		return NULL;

	val = platform_device_add_resources(pdev, tegra_ehci1_device.resource,
		tegra_ehci1_device.num_resources);
	if (val)
		goto error;

	pdev->dev.dma_mask =  tegra_ehci1_device.dev.dma_mask;
	pdev->dev.coherent_dma_mask = tegra_ehci1_device.dev.coherent_dma_mask;

	platform_data = kmalloc(sizeof(struct tegra_ehci_platform_data), GFP_KERNEL);
	if (!platform_data)
		goto error;

	memcpy(platform_data, &tegra_ehci_pdata[0],
				sizeof(struct tegra_ehci_platform_data));
	pdev->dev.platform_data = platform_data;

	val = platform_device_add(pdev);
	if (val)
		goto error_add;

	return pdev;

error_add:
	kfree(platform_data);
error:
	pr_err("%s: failed to add the host contoller device\n", __func__);
	platform_device_put(pdev);
	return NULL;
}

static void tegra_usb_otg_host_unregister(struct platform_device *pdev)
{
	kfree(pdev->dev.platform_data);
	pdev->dev.platform_data = NULL;
	platform_device_unregister(pdev);
}

static struct tegra_otg_platform_data tegra_otg_pdata = {
	.host_register = &tegra_usb_otg_host_register,
	.host_unregister = &tegra_usb_otg_host_unregister,
};

static int __init ventana_gps_init(void)
{
	struct clk *clk32 = clk_get_sys(NULL, "blink");
	if (!IS_ERR(clk32)) {
		clk_set_rate(clk32,clk32->parent->rate);
		clk_enable(clk32);
	}

	tegra_gpio_enable(TEGRA_GPIO_PZ3);
	return 0;
}

static void ventana_power_off(void)
{
	int ret;

	ret = tps6586x_power_off();
	if (ret)
		pr_err("ventana: failed to power off\n");

	while(1);
}

static void __init ventana_power_off_init(void)
{
	pm_power_off = SysShutdown;
}

#define AHB_ARBITRATION_DISABLE	0x0
#define USB_ENB			(1 << 6)
#define USB2_ENB			(1 << 18)
#define USB3_ENB			(1 << 17)

#define AHB_ARBITRATION_PRIORITY_CTRL	0x4
#define AHB_PRIORITY_WEIGHT(x)		(((x) & 0x7) << 29)
#define PRIORITY_SELEECT_USB		(1 << 6)
#define PRIORITY_SELEECT_USB2		(1 << 18)
#define PRIORITY_SELEECT_USB3		(1 << 17)

#define AHB_GIZMO_AHB_MEM		0xc
#define ENB_FAST_REARBITRATE		(1 << 2)
#define DONT_SPLIT_AHB_WR		(1 << 7)

#define AHB_GIZMO_APB_DMA		0x10

#define AHB_GIZMO_USB			0x1c
#define AHB_GIZMO_USB2			0x78
#define AHB_GIZMO_USB3			0x7c
#define IMMEDIATE			(1 << 18)
#define MAX_AHB_BURSTSIZE(x)		(((x) & 0x3) << 16)
#define DMA_BURST_1WORDS		MAX_AHB_BURSTSIZE(0)
#define DMA_BURST_4WORDS		MAX_AHB_BURSTSIZE(1)
#define DMA_BURST_8WORDS		MAX_AHB_BURSTSIZE(2)
#define DMA_BURST_16WORDS		MAX_AHB_BURSTSIZE(3)

#define AHB_MEM_PREFETCH_CFG3		0xe0
#define AHB_MEM_PREFETCH_CFG4		0xe4
#define AHB_MEM_PREFETCH_CFG1		0xec
#define AHB_MEM_PREFETCH_CFG2		0xf0
#define PREFETCH_ENB			(1 << 31)
#define MST_ID(x)			(((x) & 0x1f) << 26)
#define USB_MST_ID			MST_ID(6)
#define USB2_MST_ID			MST_ID(18)
#define USB3_MST_ID			MST_ID(17)
#define ADDR_BNDRY(x)			(((x) & 0xf) << 21)
#define INACTIVITY_TIMEOUT(x)		(((x) & 0xffff) << 0)


static inline unsigned long gizmo_readl(unsigned long offset)
{
	return readl(IO_TO_VIRT(TEGRA_AHB_GIZMO_BASE + offset));
}

static inline void gizmo_writel(unsigned long value, unsigned long offset)
{
	writel(value, IO_TO_VIRT(TEGRA_AHB_GIZMO_BASE + offset));
}

#define SERIAL_NUMBER_LENGTH 20
static char usb_serial_num[SERIAL_NUMBER_LENGTH];
static void ventana_usb_init(void)
{
	char *src = NULL;
	int i;

	unsigned long val;

	tegra_usb_phy_init(tegra_usb_phy_pdata, ARRAY_SIZE(tegra_usb_phy_pdata));
	/*enable USB1/USB2 prefetch engine*/
	val = gizmo_readl(AHB_GIZMO_AHB_MEM);
	val |= ENB_FAST_REARBITRATE | IMMEDIATE | DONT_SPLIT_AHB_WR;
	gizmo_writel(val, AHB_GIZMO_AHB_MEM);

	val = gizmo_readl(AHB_GIZMO_USB);
	val |= IMMEDIATE;
	gizmo_writel(val, AHB_GIZMO_USB);

	val = gizmo_readl(AHB_GIZMO_USB2);
	val |= IMMEDIATE;
	gizmo_writel(val, AHB_GIZMO_USB2);

	val = gizmo_readl(AHB_ARBITRATION_PRIORITY_CTRL);
	val |= PRIORITY_SELEECT_USB | PRIORITY_SELEECT_USB2 | AHB_PRIORITY_WEIGHT(7);
	gizmo_writel(val, AHB_ARBITRATION_PRIORITY_CTRL);

	val = gizmo_readl(AHB_MEM_PREFETCH_CFG1);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB | MST_ID(5) | ADDR_BNDRY(0xc) | INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(val, AHB_MEM_PREFETCH_CFG1);

	val = gizmo_readl(AHB_MEM_PREFETCH_CFG2);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB | USB_MST_ID | ADDR_BNDRY(0xc) | INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(val, AHB_MEM_PREFETCH_CFG2);

	val = gizmo_readl(AHB_MEM_PREFETCH_CFG3);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB | USB2_MST_ID | ADDR_BNDRY(0xc) | INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(val, AHB_MEM_PREFETCH_CFG3);

	tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	platform_device_register(&tegra_otg_device);

#if !defined(CONFIG_MACH_ACER_VANGOGH)
	tegra_ehci3_device.dev.platform_data=&tegra_ehci_pdata[2];
	platform_device_register(&tegra_ehci3_device);
#endif

#ifdef CONFIG_USB_ANDROID_RNDIS
	src = usb_serial_num;

	/* create a fake MAC address from our serial number.
	 * first byte is 0x02 to signify locally administered.
	 */
	rndis_pdata.ethaddr[0] = 0x02;
	for (i = 0; *src; i++) {
		/* XOR the USB serial across the remaining bytes */
		rndis_pdata.ethaddr[i % (ETH_ALEN - 1) + 1] ^= *src++;
	}
	platform_device_register(&rndis_device);
#endif
}

#ifdef CONFIG_DOCK
static void dockin_init(void)
{
	platform_device_register(&tegra_uartd_device);

	tegra_gpio_enable(TEGRA_GPIO_PR0);
	tegra_gpio_enable(TEGRA_GPIO_PR1);

	pr_info("Init dock uart device!\n");
}
#endif
static int get_pin_value(unsigned int gpio, char *name)
{
	int pin_value;

	tegra_gpio_enable(gpio);
	gpio_request(gpio, name);
	gpio_direction_input(gpio);
	pin_value = gpio_get_value(gpio);
	gpio_free(gpio);
	tegra_gpio_disable(gpio);
	return pin_value;
}

int get_sku_id(){
#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
	/* Wifi=5, 3G=3, DVT2=7 */
	return (get_pin_value(TEGRA_GPIO_PQ0, "PIN0") << 2) + \
	       (get_pin_value(TEGRA_GPIO_PQ3, "PIN1") << 1) + \
		get_pin_value(TEGRA_GPIO_PQ6, "PIN2");
#endif
#ifdef CONFIG_MACH_ACER_VANGOGH
	/* Wifi=0, 3G=1 */
	return (get_pin_value(TEGRA_GPIO_PQ0,"PIN0"));
#endif
}

static void __init tegra_ventana_init(void)
{
#if defined(CONFIG_TOUCHSCREEN_PANJIT_I2C) || \
	defined(CONFIG_TOUCHSCREEN_ATMEL_MT_T9)
	struct board_info BoardInfo;
#endif

	tegra_common_init();
	tegra_clk_init_from_table(ventana_clk_init_table);
	ventana_pinmux_init();

#ifdef CONFIG_MACH_ACER_VANGOGH
	empty_gpio_init();
#endif

	ventana_i2c_init();
	snprintf(usb_serial_num, sizeof(usb_serial_num), "%llx", tegra_chip_uid());
	andusb_plat.serial_number = kstrdup(usb_serial_num, GFP_KERNEL);
	tegra_i2s_device1.dev.platform_data = &tegra_audio_pdata[0];
	tegra_i2s_device2.dev.platform_data = &tegra_audio_pdata[1];
	tegra_spdif_device.dev.platform_data = &tegra_spdif_pdata;
	if (is_tegra_debug_uartport_hs() == true)
		platform_device_register(&tegra_uartd_device);
	else
		platform_device_register(&debug_uart);
	tegra_das_device.dev.platform_data = &tegra_das_pdata;
	tegra_ehci2_device.dev.platform_data
		= &ventana_ehci2_ulpi_platform_data;
#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
	if(get_sku_id() != 5){
		andusb_plat.product_id = USB_PID_PICA_3G_MTP_ADB;
		andusb_plat.products[0].product_id = USB_PID_PICA_3G_MTP;
		andusb_plat.products[1].product_id = USB_PID_PICA_3G_MTP_ADB;
		andusb_plat.products[2].product_id = USB_PID_PICA_3G_RNDIS;
		andusb_plat.products[3].product_id = USB_PID_PICA_3G_RNDIS_ADB;
#ifdef CONFIG_USB_ANDROID_ACCESSORY
		andusb_plat.products[4].product_id = USB_PID_PICA_3G_ACC;
		andusb_plat.products[5].product_id = USB_PID_PICA_3G_ACC_ADB;
#endif
		andusb_plat.product_name = kstrdup(USB_PRODUCT_NAME_PICA_3G, GFP_KERNEL);
	}
#elif defined(CONFIG_MACH_ACER_VANGOGH)
	if(get_sku_id() == 0){
		andusb_plat.product_id = USB_PID_VG_WIFI_MTP_ADB;
		andusb_plat.products[0].product_id = USB_PID_VG_WIFI_MTP;
		andusb_plat.products[1].product_id = USB_PID_VG_WIFI_MTP_ADB;
#ifdef CONFIG_USB_ANDROID_ACCESSORY
		andusb_plat.products[2].product_id = USB_PID_VG_WIFI_ACC;
		andusb_plat.products[3].product_id = USB_PID_VG_WIFI_ACC_ADB;
#endif
		andusb_plat.product_name = kstrdup(USB_PRODUCT_NAME_VG_WIFI, GFP_KERNEL);
	}else{
		andusb_plat.product_id = USB_PID_VG_3G_MTP_ADB;
		andusb_plat.products[0].product_id = USB_PID_VG_3G_MTP;
		andusb_plat.products[1].product_id = USB_PID_VG_3G_MTP_ADB;
		andusb_plat.products[2].product_id = USB_PID_VG_3G_RNDIS;
		andusb_plat.products[3].product_id = USB_PID_VG_3G_RNDIS_ADB;
#ifdef CONFIG_USB_ANDROID_ACCESSORY
		andusb_plat.products[4].product_id = USB_PID_VG_3G_ACC;
		andusb_plat.products[5].product_id = USB_PID_VG_3G_ACC_ADB;
#endif
		andusb_plat.product_name = kstrdup(USB_PRODUCT_NAME_VG_3G, GFP_KERNEL);
	}
#endif
	platform_add_devices(ventana_devices, ARRAY_SIZE(ventana_devices));

#ifdef CONFIG_DOCK
	if (tegra_uart_mode() < 0) {
		dockin_init();
		platform_device_register(&dock_switch);
	} else {
		pr_info("UART DEBUG MESSAGE ON!!!\n");
	}
#endif
	ventana_sdhci_init();
	ventana_charge_init();
	ventana_regulator_init();

#if defined(CONFIG_TOUCHSCREEN_PANJIT_I2C) || \
	defined(CONFIG_TOUCHSCREEN_ATMEL_MT_T9)

	tegra_get_board_info(&BoardInfo);

	/* boards with sku > 0 have atmel touch panels */
	if (BoardInfo.sku) {
		pr_info("Initializing Atmel touch driver\n");
		ventana_touch_init_atmel();
	} else {
#if defined(CONFIG_TOUCHSCREEN_PANJIT_I2C)
		pr_info("Initializing Panjit touch driver\n");
		ventana_touch_init_panjit();
#endif
	}
#endif
#ifdef CONFIG_TOUCHSCREEN_CYPRESS
	ventana_touch_init_cypress();
#endif

#ifdef CONFIG_KEYBOARD_GPIO
	ventana_keys_init();
#endif
#ifdef CONFIG_KEYBOARD_TEGRA
	ventana_kbc_init();
#endif

	ventana_wired_jack_init();
	ventana_usb_init();

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA)
	g_sku_id = get_sku_id();
	pr_info("g_sku_id=%d (sku_id_5 does not mount USB.2 PHY)", (int)g_sku_id);

	if(g_sku_id!=5 ){
		/*3G sku, do ehci2 init, simdetect*/
		platform_device_register(&tegra_ehci2_device);
#ifdef CONFIG_SIMDETECT
		platform_device_register(&picasso_simdetect_switch);
#endif
	}else{
		/*WIFI sku does not mount PHY, we do not init ehci2 for WIFI sku*/
	}
#endif

// Compal, Vangogh enable ehci interface for 3G sku (factory)
#ifdef CONFIG_MACH_ACER_VANGOGH
	g_sku_id = get_sku_id();
	if(g_sku_id!=0 ){
		platform_device_register(&tegra_ehci2_device);
#ifdef CONFIG_SIMDETECT
		platform_device_register(&picasso_simdetect_switch);
#endif
	}
#endif

	ventana_gps_init();
	ventana_panel_init();
	ventana_sensors_init();
	ventana_bt_rfkill();
	ventana_power_off_init();
	ventana_emc_init();
#ifdef CONFIG_BT_BLUESLEEP
	tegra_setup_bluesleep();
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
	tegra_gpio_enable(TEGRA_GPIO_PV5);
#endif
#ifdef CONFIG_ROTATELOCK
	tegra_gpio_enable(TEGRA_GPIO_PQ2);
#endif
#ifdef CONFIG_PSENSOR
	// enable gpio for psensor
	tegra_gpio_enable(TEGRA_GPIO_PC1);
#endif
#ifdef CONFIG_SIMDETECT
        // enable gpio for sim detection
        tegra_gpio_enable(TEGRA_GPIO_PI7);
#endif
#ifdef CONFIG_DOCK
	tegra_gpio_enable(TEGRA_GPIO_PX6);
#endif
	pr_err("sku_id: %d", get_sku_id());
}

int __init tegra_ventana_protected_aperture_init(void)
{
	tegra_protected_aperture_init(tegra_grhost_aperture);
	return 0;
}
late_initcall(tegra_ventana_protected_aperture_init);

void __init tegra_ventana_reserve(void)
{
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

	if( total_ram_size == 0x20000000)
		tegra_reserve(SZ_128M, SZ_8M, SZ_16M);
	else
		tegra_reserve(SZ_256M, SZ_8M, SZ_16M);
}

/*
 * TO-DO: This should be fixed if bootloader can pass correct
 *        machine id to kernel.
 */
MACHINE_START(VENTANA, "picasso")
	.boot_params    = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_ventana_init,
	.map_io         = tegra_map_common_io,
	.reserve        = tegra_ventana_reserve,
	.timer          = &tegra_timer,
MACHINE_END

MACHINE_START(PICASSO, "picasso")
	.boot_params    = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_ventana_init,
	.map_io         = tegra_map_common_io,
	.reserve        = tegra_ventana_reserve,
	.timer          = &tegra_timer,
MACHINE_END

MACHINE_START(MAYA, "maya")
	.boot_params    = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_ventana_init,
	.map_io         = tegra_map_common_io,
	.reserve        = tegra_ventana_reserve,
	.timer          = &tegra_timer,
MACHINE_END

MACHINE_START(VANGOGH, "vangogh")
	.boot_params    = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_ventana_init,
	.map_io         = tegra_map_common_io,
	.reserve        = tegra_ventana_reserve,
	.timer          = &tegra_timer,
MACHINE_END
