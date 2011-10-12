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

extern int wired_jack_state(void);

#endif
