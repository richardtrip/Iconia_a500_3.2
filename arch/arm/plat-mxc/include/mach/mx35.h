#ifndef __MACH_MX35_H__
#define __MACH_MX35_H__
/*
 * IRAM
 */
#define MX35_IRAM_BASE_ADDR		0x10000000	/* internal ram */
#define MX35_IRAM_SIZE			SZ_128K

#define MX35_L2CC_BASE_ADDR		0x30000000
#define MX35_L2CC_SIZE			SZ_1M

#define MX35_AIPS1_BASE_ADDR		0x43f00000
#define MX35_AIPS1_BASE_ADDR_VIRT	0xfc000000
#define MX35_AIPS1_SIZE			SZ_1M
#define MX35_MAX_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x04000)
#define MX35_EVTMON_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x08000)
#define MX35_CLKCTL_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x0c000)
#define MX35_ETB_SLOT4_BASE_ADDR		(MX35_AIPS1_BASE_ADDR + 0x10000)
#define MX35_ETB_SLOT5_BASE_ADDR		(MX35_AIPS1_BASE_ADDR + 0x14000)
#define MX35_ECT_CTIO_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x18000)
#define MX35_I2C1_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x80000)
#define MX35_I2C3_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x84000)
#define MX35_UART1_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x90000)
#define MX35_UART2_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x94000)
#define MX35_I2C2_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x98000)
#define MX35_OWIRE_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0x9c000)
#define MX35_SSI1_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0xa0000)
#define MX35_CSPI1_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0xa4000)
#define MX35_KPP_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0xa8000)
#define MX35_IOMUXC_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0xac000)
#define MX35_ECT_IP1_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0xb8000)
#define MX35_ECT_IP2_BASE_ADDR			(MX35_AIPS1_BASE_ADDR + 0xbc000)

#define MX35_SPBA0_BASE_ADDR		0x50000000
#define MX35_SPBA0_BASE_ADDR_VIRT	0xfc100000
#define MX35_SPBA0_SIZE			SZ_1M
#define MX35_UART3_BASE_ADDR			(MX35_SPBA0_BASE_ADDR + 0x0c000)
#define MX35_CSPI2_BASE_ADDR			(MX35_SPBA0_BASE_ADDR + 0x10000)
#define MX35_SSI2_BASE_ADDR			(MX35_SPBA0_BASE_ADDR + 0x14000)
#define MX35_ATA_DMA_BASE_ADDR			(MX35_SPBA0_BASE_ADDR + 0x20000)
#define MX35_MSHC1_BASE_ADDR			(MX35_SPBA0_BASE_ADDR + 0x24000)
#define MX35_FEC_BASE_ADDR		0x50038000
#define MX35_SPBA_CTRL_BASE_ADDR		(MX35_SPBA0_BASE_ADDR + 0x3c000)

#define MX35_AIPS2_BASE_ADDR		0x53f00000
#define MX35_AIPS2_BASE_ADDR_VIRT	0xfc200000
#define MX35_AIPS2_SIZE			SZ_1M
#define MX35_CCM_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0x80000)
#define MX35_GPT1_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0x90000)
#define MX35_EPIT1_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0x94000)
#define MX35_EPIT2_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0x98000)
#define MX35_GPIO3_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xa4000)
#define MX35_SCC_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xac000)
#define MX35_RNGA_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xb0000)
#define MX35_IPU_CTRL_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xc0000)
#define MX35_AUDMUX_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xc4000)
#define MX35_GPIO1_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xcc000)
#define MX35_GPIO2_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xd0000)
#define MX35_SDMA_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xd4000)
#define MX35_RTC_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xd8000)
#define MX35_WDOG_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xdc000)
#define MX35_PWM_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xe0000)
#define MX35_CAN1_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xe4000)
#define MX35_CAN2_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xe8000)
#define MX35_RTIC_BASE_ADDR			(MX35_AIPS2_BASE_ADDR + 0xec000)
#define MX35_OTG_BASE_ADDR		0x53ff4000

#define MX35_ROMP_BASE_ADDR		0x60000000
#define MX35_ROMP_BASE_ADDR_VIRT	0xfc500000
#define MX35_ROMP_SIZE			SZ_1M

#define MX35_AVIC_BASE_ADDR		0x68000000
#define MX35_AVIC_BASE_ADDR_VIRT	0xfc400000
#define MX35_AVIC_SIZE			SZ_1M

/*
 * Memory regions and CS
 */
#define MX35_IPU_MEM_BASE_ADDR		0x70000000
#define MX35_CSD0_BASE_ADDR		0x80000000
#define MX35_CSD1_BASE_ADDR		0x90000000

#define MX35_CS0_BASE_ADDR		0xa0000000
#define MX35_CS1_BASE_ADDR		0xa8000000
#define MX35_CS2_BASE_ADDR		0xb0000000
#define MX35_CS3_BASE_ADDR		0xb2000000

#define MX35_CS4_BASE_ADDR		0xb4000000
#define MX35_CS4_BASE_ADDR_VIRT		0xf4000000
#define MX35_CS4_SIZE			SZ_32M

#define MX35_CS5_BASE_ADDR		0xb6000000
#define MX35_CS5_BASE_ADDR_VIRT		0xf6000000
#define MX35_CS5_SIZE			SZ_32M

/*
 * NAND, SDRAM, WEIM, M3IF, EMI controllers
 */
#define MX35_X_MEMC_BASE_ADDR		0xb8000000
#define MX35_X_MEMC_BASE_ADDR_VIRT	0xfc320000
#define MX35_X_MEMC_SIZE		SZ_64K
#define MX35_ESDCTL_BASE_ADDR			(MX35_X_MEMC_BASE_ADDR + 0x1000)
#define MX35_WEIM_BASE_ADDR			(MX35_X_MEMC_BASE_ADDR + 0x2000)
#define MX35_M3IF_BASE_ADDR			(MX35_X_MEMC_BASE_ADDR + 0x3000)
#define MX35_EMI_CTL_BASE_ADDR			(MX35_X_MEMC_BASE_ADDR + 0x4000)
#define MX35_PCMCIA_CTL_BASE_ADDR		MX35_EMI_CTL_BASE_ADDR

#define MX35_NFC_BASE_ADDR		0xbb000000
#define MX35_PCMCIA_MEM_BASE_ADDR	0xbc000000

#define MX35_IO_ADDRESS(x) (						\
	IMX_IO_ADDRESS(x, MX35_AIPS1) ?:				\
	IMX_IO_ADDRESS(x, MX35_AIPS2) ?:				\
	IMX_IO_ADDRESS(x, MX35_AVIC) ?:					\
	IMX_IO_ADDRESS(x, MX35_X_MEMC) ?:				\
	IMX_IO_ADDRESS(x, MX35_SPBA0))

/*
 * Interrupt numbers
 */
#define MX35_INT_OWIRE		2
#define MX35_INT_I2C3		3
#define MX35_INT_I2C2		4
#define MX35_INT_RTIC		6
#define MX35_INT_MMC_SDHC1	7
#define MX35_INT_MMC_SDHC2	8
#define MX35_INT_MMC_SDHC3	9
#define MX35_INT_I2C1		10
#define MX35_INT_SSI1		11
#define MX35_INT_SSI2		12
#define MX35_INT_CSPI2		13
#define MX35_INT_CSPI1		14
#define MX35_INT_ATA		15
#define MX35_INT_GPU2D		16
#define MX35_INT_ASRC		17
#define MX35_INT_UART3		18
#define MX35_INT_IIM		19
#define MX35_INT_RNGA		22
#define MX35_INT_EVTMON		23
#define MX35_INT_KPP		24
#define MX35_INT_RTC		25
#define MX35_INT_PWM		26
#define MX35_INT_EPIT2		27
#define MX35_INT_EPIT1		28
#define MX35_INT_GPT		29
#define MX35_INT_POWER_FAIL	30
#define MX35_INT_UART2		32
#define MX35_INT_NANDFC		33
#define MX35_INT_SDMA		34
#define MX35_INT_USBHS		35
#define MX35_INT_USBOTG		37
#define MX35_INT_MSHC1		39
#define MX35_INT_ESAI		40
#define MX35_INT_IPU_ERR	41
#define MX35_INT_IPU_SYN	42
#define MX35_INT_CAN1		43
#define MX35_INT_CAN2		44
#define MX35_INT_UART1		45
#define MX35_INT_MLB		46
#define MX35_INT_SPDIF		47
#define MX35_INT_ECT		48
#define MX35_INT_SCC_SCM	49
#define MX35_INT_SCC_SMN	50
#define MX35_INT_GPIO2		51
#define MX35_INT_GPIO1		52
#define MX35_INT_WDOG		55
#define MX35_INT_GPIO3		56
#define MX35_INT_FEC		57
#define MX35_INT_EXT_POWER	58
#define MX35_INT_EXT_TEMPER	59
#define MX35_INT_EXT_SENSOR60	60
#define MX35_INT_EXT_SENSOR61	61
#define MX35_INT_EXT_WDOG	62
#define MX35_INT_EXT_TV		63

#define MX35_PROD_SIGNATURE		0x1	/* For MX31 */

/* silicon revisions specific to i.MX31 */
#define MX35_CHIP_REV_1_0		0x10
#define MX35_CHIP_REV_1_1		0x11
#define MX35_CHIP_REV_1_2		0x12
#define MX35_CHIP_REV_1_3		0x13
#define MX35_CHIP_REV_2_0		0x20
#define MX35_CHIP_REV_2_1		0x21
#define MX35_CHIP_REV_2_2		0x22
#define MX35_CHIP_REV_2_3		0x23
#define MX35_CHIP_REV_3_0		0x30
#define MX35_CHIP_REV_3_1		0x31
#define MX35_CHIP_REV_3_2		0x32

#define MX35_SYSTEM_REV_MIN		MX35_CHIP_REV_1_0
#define MX35_SYSTEM_REV_NUM		3

#ifdef IMX_NEEDS_DEPRECATED_SYMBOLS
/* these should go away */
#define MXC_FEC_BASE_ADDR MX35_FEC_BASE_ADDR
#define MXC_INT_OWIRE MX35_INT_OWIRE
#define MXC_INT_MMC_SDHC2 MX35_INT_MMC_SDHC2
#define MXC_INT_MMC_SDHC3 MX35_INT_MMC_SDHC3
#define MXC_INT_GPU2D MX35_INT_GPU2D
#define MXC_INT_ASRC MX35_INT_ASRC
#define MXC_INT_USBHS MX35_INT_USBHS
#define MXC_INT_USBOTG MX35_INT_USBOTG
#define MXC_INT_ESAI MX35_INT_ESAI
#define MXC_INT_CAN1 MX35_INT_CAN1
#define MXC_INT_CAN2 MX35_INT_CAN2
#define MXC_INT_MLB MX35_INT_MLB
#define MXC_INT_SPDIF MX35_INT_SPDIF
#define MXC_INT_FEC MX35_INT_FEC
#endif

#endif /* ifndef __MACH_MX35_H__ */
