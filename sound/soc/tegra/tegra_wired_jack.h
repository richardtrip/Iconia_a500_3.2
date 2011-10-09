/*
 * tegra_wired_jack.h  --  SoC audio for tegra wired jack
 *
 */

#ifndef __TEGRA_WIRED_JACK__
#define __TEGRA_WIRED_JACK__

#define HEAD_DET_GPIO 0

/* wm8903 codec register values */
#define B07_INEMUTE             7
#define B06_VOL_M3DB            6
#define B00_IN_VOL              0
#define B00_INR_ENA             0
#define B01_INL_ENA             1
#define R06_MICBIAS_CTRL_0      6
#define B07_MICDET_HYST_ENA     7
#define B04_MICDET_THR          4
#define B02_MICSHORT_THR        2
#define B01_MICDET_ENA          1
#define B00_MICBIAS_ENA         0
#define B15_DRC_ENA             15
#define B03_DACL_ENA            3
#define B02_DACR_ENA            2
#define B01_ADCL_ENA            1
#define B00_ADCR_ENA            0
#define B06_IN_CM_ENA           6
#define B04_IP_SEL_N            4
#define B02_IP_SEL_P            2
#define B00_MODE                0
#define B06_AIF_ADCL            7
#define B06_AIF_ADCR            6
#define B05_ADC_HPF_CUT         5
#define B04_ADC_HPF_ENA         4
#define B01_ADCL_DATINV         1
#define B00_ADCR_DATINV         0
#define R20_SIDETONE_CTRL       32
#define R29_DRC_1               41

#define JACK_NONE               0
#define JACK_HEADSET            1
#define JACK_HEADPHONE          2

/* Register defaults at reset */
static u16 wm8903_reg_defaults[] = {
	0x8903,     /* R0   - SW Reset and ID */
	0x0000,     /* R1   - Revision Number */
	0x0000,     /* R2 */
	0x0000,     /* R3 */
	0x0018,     /* R4   - Bias Control 0 */
	0x0000,     /* R5   - VMID Control 0 */
	0x0000,     /* R6   - Mic Bias Control 0 */
	0x0000,     /* R7 */
	0x0001,     /* R8   - Analogue DAC 0 */
	0x0000,     /* R9 */
	0x0001,     /* R10  - Analogue ADC 0 */
	0x0000,     /* R11 */
	0x0000,     /* R12  - Power Management 0 */
	0x0000,     /* R13  - Power Management 1 */
	0x0000,     /* R14  - Power Management 2 */
	0x0000,     /* R15  - Power Management 3 */
	0x0000,     /* R16  - Power Management 4 */
	0x0000,     /* R17  - Power Management 5 */
	0x0000,     /* R18  - Power Management 6 */
	0x0000,     /* R19 */
	0x0400,     /* R20  - Clock Rates 0 */
	0x0D07,     /* R21  - Clock Rates 1 */
	0x0000,     /* R22  - Clock Rates 2 */
	0x0000,     /* R23 */
	0x0050,     /* R24  - Audio Interface 0 */
	0x0242,     /* R25  - Audio Interface 1 */
	0x0008,     /* R26  - Audio Interface 2 */
	0x0022,     /* R27  - Audio Interface 3 */
	0x0000,     /* R28 */
	0x0000,     /* R29 */
	0x00C0,     /* R30  - DAC Digital Volume Left */
	0x00C0,     /* R31  - DAC Digital Volume Right */
	0x0000,     /* R32  - DAC Digital 0 */
	0x0000,     /* R33  - DAC Digital 1 */
	0x0000,     /* R34 */
	0x0000,     /* R35 */
	0x00C0,     /* R36  - ADC Digital Volume Left */
	0x00C0,     /* R37  - ADC Digital Volume Right */
	0x0000,     /* R38  - ADC Digital 0 */
	0x0073,     /* R39  - Digital Microphone 0 */
	0x09BF,     /* R40  - DRC 0 */
	0x3241,     /* R41  - DRC 1 */
	0x0020,     /* R42  - DRC 2 */
	0x0000,     /* R43  - DRC 3 */
	0x0085,     /* R44  - Analogue Left Input 0 */
	0x0085,     /* R45  - Analogue Right Input 0 */
	0x0044,     /* R46  - Analogue Left Input 1 */
	0x0044,     /* R47  - Analogue Right Input 1 */
	0x0000,     /* R48 */
	0x0000,     /* R49 */
	0x0008,     /* R50  - Analogue Left Mix 0 */
	0x0004,     /* R51  - Analogue Right Mix 0 */
	0x0000,     /* R52  - Analogue Spk Mix Left 0 */
	0x0000,     /* R53  - Analogue Spk Mix Left 1 */
	0x0000,     /* R54  - Analogue Spk Mix Right 0 */
	0x0000,     /* R55  - Analogue Spk Mix Right 1 */
	0x0000,     /* R56 */
	0x002D,     /* R57  - Analogue OUT1 Left */
	0x002D,     /* R58  - Analogue OUT1 Right */
	0x0039,     /* R59  - Analogue OUT2 Left */
	0x0039,     /* R60  - Analogue OUT2 Right */
	0x0100,     /* R61 */
	0x0139,     /* R62  - Analogue OUT3 Left */
	0x0139,     /* R63  - Analogue OUT3 Right */
	0x0000,     /* R64 */
	0x0000,     /* R65  - Analogue SPK Output Control 0 */
	0x0000,     /* R66 */
	0x0010,     /* R67  - DC Servo 0 */
	0x0100,     /* R68 */
	0x00A4,     /* R69  - DC Servo 2 */
	0x0807,     /* R70 */
	0x0000,     /* R71 */
	0x0000,     /* R72 */
	0x0000,     /* R73 */
	0x0000,     /* R74 */
	0x0000,     /* R75 */
	0x0000,     /* R76 */
	0x0000,     /* R77 */
	0x0000,     /* R78 */
	0x000E,     /* R79 */
	0x0000,     /* R80 */
	0x0000,     /* R81 */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0006,     /* R87 */
	0x0000,     /* R88 */
	0x0000,     /* R89 */
	0x0000,     /* R90  - Analogue HP 0 */
	0x0060,     /* R91 */
	0x0000,     /* R92 */
	0x0000,     /* R93 */
	0x0000,     /* R94  - Analogue Lineout 0 */
	0x0060,     /* R95 */
	0x0000,     /* R96 */
	0x0000,     /* R97 */
	0x0000,     /* R98  - Charge Pump 0 */
	0x1F25,     /* R99 */
	0x2B19,     /* R100 */
	0x01C0,     /* R101 */
	0x01EF,     /* R102 */
	0x2B00,     /* R103 */
	0x0000,     /* R104 - Class W 0 */
	0x01C0,     /* R105 */
	0x1C10,     /* R106 */
	0x0000,     /* R107 */
	0x0000,     /* R108 - Write Sequencer 0 */
	0x0000,     /* R109 - Write Sequencer 1 */
	0x0000,     /* R110 - Write Sequencer 2 */
	0x0000,     /* R111 - Write Sequencer 3 */
	0x0000,     /* R112 - Write Sequencer 4 */
	0x0000,     /* R113 */
	0x0000,     /* R114 - Control Interface */
	0x0000,     /* R115 */
	0x00A8,     /* R116 - GPIO Control 1 */
	0x00A8,     /* R117 - GPIO Control 2 */
	0x00A8,     /* R118 - GPIO Control 3 */
	0x0220,     /* R119 - GPIO Control 4 */
	0x01A0,     /* R120 - GPIO Control 5 */
	0x0000,     /* R121 - Interrupt Status 1 */
	0xFFFF,     /* R122 - Interrupt Status 1 Mask */
	0x0000,     /* R123 - Interrupt Polarity 1 */
	0x0000,     /* R124 */
	0x0003,     /* R125 */
	0x0000,     /* R126 - Interrupt Control */
	0x0000,     /* R127 */
	0x0005,     /* R128 */
	0x0000,     /* R129 - Control Interface Test 1 */
	0x0000,     /* R130 */
	0x0000,     /* R131 */
	0x0000,     /* R132 */
	0x0000,     /* R133 */
	0x0000,     /* R134 */
	0x03FF,     /* R135 */
	0x0007,     /* R136 */
	0x0040,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0000,     /* R140 */
	0x0000,     /* R141 */
	0x0000,     /* R142 */
	0x0000,     /* R143 */
	0x0000,     /* R144 */
	0x0000,     /* R145 */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x4000,     /* R148 */
	0x6810,     /* R149 - Charge Pump Test 1 */
	0x0004,     /* R150 */
	0x0000,     /* R151 */
	0x0000,     /* R152 */
	0x0000,     /* R153 */
	0x0000,     /* R154 */
	0x0000,     /* R155 */
	0x0000,     /* R156 */
	0x0000,     /* R157 */
	0x0000,     /* R158 */
	0x0000,     /* R159 */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 */
	0x0028,     /* R164 - Clock Rate Test 4 */
	0x0004,     /* R165 */
	0x0000,     /* R166 */
	0x0060,     /* R167 */
	0x0000,     /* R168 */
	0x0000,     /* R169 */
	0x0000,     /* R170 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Analogue Output Bias 0 */
};

struct wm8903_priv {
	struct snd_soc_codec codec;
	u16 reg_cache[ARRAY_SIZE(wm8903_reg_defaults)];

	int sysclk;

	/* Reference counts */
	int class_w_users;
	int playback_active;
	int capture_active;

	struct completion wseq;

	struct snd_soc_jack *mic_jack;
	int mic_det;
	int mic_short;
	int mic_last_report;
	int mic_delay;

	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;

#if defined(CONFIG_MACH_ACER_PICASSO) || defined(CONFIG_MACH_ACER_MAYA) || defined(CONFIG_MACH_ACER_VANGOGH)
	struct work_struct work;
	int amp_status;
	int amp_event;

	int fm2018_enable;
	int fm2018_status;
#endif

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif
};

extern int wired_jack_state(void);
void select_mic_input(int state);

#endif
