/*
 * drivers/power/EC_Bat.c
 *
 * Gas Gauge driver for TI's BQ20Z75
 *
 * Copyright (c) 2010, NVIDIA Corporation.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/interrupt.h>

#define NVBATTERY_POLLING_INTERVAL 30000 /* 30 Seconds */
#define ECBATTERY_RESUME_INTERVAL 500 /* 0.5 Seconds */

static enum power_supply_property EC_Bat_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};

extern int checkLCM(void);

static enum power_supply_property EC_Bat_charge_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int EC_Bat_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val);

static int EC_Bat_get_charge_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val);

static s32 i2c_smbus_read_word_data_retry(struct i2c_client *client, u8 command);
static s32 i2c_smbus_write_word_data_retry(struct i2c_client *client, u8 command, u16 value);

static char *supply_list[] = {
	"battery",
};

static struct power_supply EC_Bat_supply[] = {
{
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = EC_Bat_properties,
	.num_properties = ARRAY_SIZE(EC_Bat_properties),
	.get_property = EC_Bat_get_property,
},
{
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.supplied_to = supply_list,
	.num_supplicants = ARRAY_SIZE(supply_list),
	.properties = EC_Bat_charge_properties,
	.num_properties = ARRAY_SIZE(EC_Bat_charge_properties),
	.get_property = EC_Bat_get_charge_property,
},

};
struct EC_Bat_device_info {
	struct i2c_client	*client;
} *EC_Bat_device;

int acinout = 0;
struct timer_list poll_timer;
unsigned int batt_status_poll_period;
int gpio;
s16 ThreeGPower_val = 2;
s16 MicSwitch_val = 2;
s16 Coldboot_val = 0;
s16 Resume_val = 0;
s16 GPS_val = 0;
s16 PsensorPower_val = 2;
bool ECflashMode = 0;//for EC flash
s32 CurrCapacity = 0;
#ifdef CONFIG_MACH_ACER_VANGOGH
s16 homekeyled_val = 0;
#endif

typedef enum
{
	NvCharger_Type_Battery = 0,
	NvCharger_Type_AC,
}NvCharger_Type;

static void tegra_battery_poll_timer_func(unsigned long unused)
{
	acinout = 0;
	power_supply_changed(&EC_Bat_supply[NvCharger_Type_Battery]);
	mod_timer(&poll_timer, jiffies + msecs_to_jiffies(batt_status_poll_period));
}

static irqreturn_t ac_interrupt(int irq, void *dev_id)
{
	acinout = 1;
	if(ECflashMode == 0)//disable polling EC for battery info when ECflash
	{
		power_supply_changed(&EC_Bat_supply[NvCharger_Type_Battery]);
		power_supply_changed(&EC_Bat_supply[NvCharger_Type_AC]);
	}
	return IRQ_HANDLED;
}

//SYS_START
static struct kobject *Ecdebug_kobj;


#define debug_attr(_name) \
	static struct kobj_attribute _name##_attr = { \
	.attr = { \
	.name = __stringify(_name), \
	.mode = 0644, \
	}, \
	.show = _name##_show, \
	.store = _name##_store, \
	}

static int atoi(const char *a)
{
	int s = 0;
	while(*a >= '0' && *a <= '9')
		s = (s << 3) + (s << 1) + *a++ - '0';
	return s;
}

static ssize_t PowerLED_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "PowerLED");
	return (s - buf);
}

static ssize_t PowerLED_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x42,0);
	msleep(100);
	return n;
}

static ssize_t ChargeLED_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "ChargeLED");
	return (s - buf);
}

static ssize_t ChargeLED_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x43,0);
	msleep(100);
	return n;
}

static ssize_t OriStsLED_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "OriStsLED");
	return (s - buf);
}

static ssize_t OriStsLED_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x40,0);
	msleep(100);
	return n;
}

static ssize_t OffLED_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "OffLED");
	return (s - buf);
}

static ssize_t OffLED_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x41,0);
	msleep(100);
	return n;
}

static ssize_t BatCtlEnable_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 ret;
	s8 byteret;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x5b);
	byteret = ret & 0x000000FF;
	s += sprintf(s, "%d\n",byteret);
	return (s - buf);
}

static ssize_t BatCtlEnable_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	s16 BatCtlEnable_val;
	buffer = atoi(buf);
	BatCtlEnable_val = buffer & 0x0000FFFF;
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x50,BatCtlEnable_val);
	msleep(100);
	return n;
}

static ssize_t BatCtlDisable_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "BatCtlDisable");
	return (s - buf);
}

static ssize_t BatCtlDisable_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x51,0);
	msleep(100);
	return n;
}

static ssize_t EcVer_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 PjIDMaVer, MiVerTestVer, ret;
	PjIDMaVer = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x30);
	msleep(100);
	MiVerTestVer = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x31);
	msleep(100);
	ret = PjIDMaVer;
	ret = ret << 16 | (MiVerTestVer & 0xFFFF);
	s += sprintf(s, "%x\n", ret);
	return (s - buf);
}

static ssize_t EcVer_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	return n;
}

static void TransformToByte(u16 val16, u8 *val8_0, u8 *val8_1)
{
	*val8_0 = val16 & 0x00ff;
	*val8_1 = val16 >> 8 & 0x00ff;
}

static ssize_t UUID_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	int i;
	char * s = buf;
	u8 val8[16] = {0};
	u16 val16;
	s32 val32;

	for(i=0;i<=7;i++)
	{
		val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x60);
		val16 = val32 & 0x0000ffff;
		TransformToByte(val16, &val8[2*i], &val8[2*i+1]);
		msleep(10);
	}
	s += sprintf(s, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",val8[15],val8[14],val8[13],val8[12],val8[11],val8[10],val8[9],val8[8],val8[7],val8[6],val8[5],val8[4],val8[3],val8[2],val8[1],val8[0]);

	return (s - buf);
}

static u8 TransformCharToByte(const char *buf, int index)
{
	u8  val8;
	char locbuf[2] = {0};
	int buffer = 0, j;

	for(j=0;j<=1;j++)
	{
		locbuf[j] = buf[index+j];
	}

	if(locbuf[1] == 'a' || locbuf[1] == 'A')
		buffer = 10;
	else if(locbuf[1] == 'b' || locbuf[1] == 'B')
		buffer = 11;
	else if(locbuf[1] == 'c' || locbuf[1] == 'C')
		buffer = 12;
	else if(locbuf[1] == 'd' || locbuf[1] == 'D')
		buffer = 13;
	else if(locbuf[1] == 'e' || locbuf[1] == 'E')
		buffer = 14;
	else if(locbuf[1] == 'f' || locbuf[1] == 'F')
		buffer = 15;
	else if(locbuf[1] == '0')
		buffer = 0;
	else if(locbuf[1] == '1')
		buffer = 1;
	else if(locbuf[1] == '2')
		buffer = 2;
	else if(locbuf[1] == '3')
		buffer = 3;
	else if(locbuf[1] == '4')
		buffer = 4;
	else if(locbuf[1] == '5')
		buffer = 5;
	else if(locbuf[1] == '6')
		buffer = 6;
	else if(locbuf[1] == '7')
		buffer = 7;
	else if(locbuf[1] == '8')
		buffer = 8;
	else if(locbuf[1] == '9')
		buffer = 9;

	if(locbuf[0] == 'a' || locbuf[0] == 'A')
		buffer += 10*16;
	else if(locbuf[0] == 'b' || locbuf[0] == 'B')
		buffer += 11*16;
	else if(locbuf[0] == 'c' || locbuf[0] == 'C')
		buffer += 12*16;
	else if(locbuf[0] == 'd' || locbuf[0] == 'D')
		buffer += 13*16;
	else if(locbuf[0] == 'e' || locbuf[0] == 'E')
		buffer += 14*16;
	else if(locbuf[0] == 'f' || locbuf[0] == 'F')
		buffer += 15*16;
	else if(locbuf[0] == '0')
		buffer += 0*16;
	else if(locbuf[0] == '1')
		buffer += 1*16;
	else if(locbuf[0] == '2')
		buffer += 2*16;
	else if(locbuf[0] == '3')
		buffer += 3*16;
	else if(locbuf[0] == '4')
		buffer += 4*16;
	else if(locbuf[0] == '5')
		buffer += 5*16;
	else if(locbuf[0] == '6')
		buffer += 6*16;
	else if(locbuf[0] == '7')
		buffer += 7*16;
	else if(locbuf[0] == '8')
		buffer += 8*16;
	else if(locbuf[0] == '9')
		buffer += 9*16;

	val8 = buffer & 0x000000ff;

	return val8;
}

static u16 TransformToWord(u8 val8_0, u8 val8_1)
{
	u16 val16;
	val16 = val8_1;

	val16 = val16 << 8 | val8_0;

	return val16;
}

static ssize_t UUID_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;
	int i;

	for(i=0;i<=7;i++)
	{
		val8_0 = TransformCharToByte(buf,30-4*i);
		val8_1 = TransformCharToByte(buf,28-4*i);
		val16 = TransformToWord(val8_0, val8_1);
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x61,val16);
		msleep(10);
	}

	return n;
}

static ssize_t BatCapacity_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 ret;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x00);
	msleep(10);
	s += sprintf(s, "%d\n", ret);

	return (s - buf);
}

static ssize_t BatCapacity_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	return n;
}

static ssize_t BatDesignCapacity_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 ret;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x08);
	msleep(100);
	s += sprintf(s, "%d\n", ret);

	return (s - buf);
}

static ssize_t BatDesignCapacity_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	return n;
}

static ssize_t BTMAC_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	int i;
	char * s = buf;
	u8 val8[6] = {0};
	u16 val16;
	s32 val32;

	for(i=0;i<=2;i++)
	{
		val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x62);
		val16 = val32 & 0x0000ffff;
		TransformToByte(val16, &val8[2*i], &val8[2*i+1]);
		msleep(10);
	}

	s += sprintf(s, "%02x%02x%02x%02x%02x%02x\n",val8[5],val8[4],val8[3],val8[2],val8[1],val8[0]);

	return (s - buf);
}

static ssize_t BTMAC_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;
	int i;

	for(i=0;i<=2;i++)
	{
		val8_0 = TransformCharToByte(buf,10-4*i);
		val8_1 = TransformCharToByte(buf,8-4*i);
		val16 = TransformToWord(val8_0, val8_1);
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x63,val16);
		msleep(10);
	}

	return n;
}

static ssize_t WIFIMAC_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	int i;
	char * s = buf;
	u8 val8[6] = {0};
	u16 val16;
	s32 val32;

	for(i=0;i<=2;i++)
	{
		val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x64);
		val16 = val32 & 0x0000ffff;
		TransformToByte(val16, &val8[2*i], &val8[2*i+1]);
		msleep(10);
	}

	s += sprintf(s, "%02x%02x%02x%02x%02x%02x\n",val8[5],val8[4],val8[3],val8[2],val8[1],val8[0]);

	return (s - buf);
}

static ssize_t WIFIMAC_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;
	int i;

	for(i=0;i<=2;i++)
	{
		val8_0 = TransformCharToByte(buf,10-4*i);
		val8_1 = TransformCharToByte(buf,8-4*i);

		val16 = TransformToWord(val8_0, val8_1);
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x65,val16);
		msleep(10);
	}

	return n;
}


static ssize_t BatStatus_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	int ACStatus;
	s32 Capacity, Present;
	Present = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x08);//read designcapacity to judge present
	msleep(100);
	Capacity = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x00);//read capacity
	ACStatus = gpio_get_value(gpio);//read ac exist gpio, 0 is exist

	if(Present == 0)
	{
		s += sprintf(s, "Absence\n");
	}
	else
	{
		if(Capacity < 100)
		{
			if(ACStatus == 1)
				s += sprintf(s, "Discharging\n");
			else
				s += sprintf(s, "Charging\n");
		}
		else
		{
			s += sprintf(s, "Full\n");
		}
	}
	msleep(100);

	return (s - buf);
}

static ssize_t BatStatus_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	return n;
}

static ssize_t ECRead_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	u16 val16;
	s32 val32;

	val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0xF1);
	val16 = val32 & 0x0000ffff;

	s += sprintf(s, "0x%04x\n",val16);

	return (s - buf);
}

//echo addresshbyte_addresslbyte > ECRead
static ssize_t ECRead_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;

	val8_0 = TransformCharToByte(buf,2);
	val8_1 = TransformCharToByte(buf,0);

	val16 = TransformToWord(val8_0, val8_1);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0xF0,val16);

	return n;
}

static ssize_t ECWrite_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "ECWrite");

	return (s - buf);
}

//echo addresshbyte_addresslbyte_val > ECWrite
static ssize_t ECWrite_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;
	val8_0 = TransformCharToByte(buf,2);
	val8_1 = TransformCharToByte(buf,0);
	val16 = TransformToWord(val8_0, val8_1);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0xF0,val16);   //write address
	val8_1 = 0;
	val8_0 = TransformCharToByte(buf,4);
	val16 = TransformToWord(val8_0, val8_1);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0xF2,val16);   //write value 00xx

	return n;
}


static ssize_t Shutdown_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "Shutdown");

	return (s - buf);
}

static ssize_t Shutdown_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x52,0);
	msleep(100);

	return n;
}

static ssize_t Suspend_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "Suspend");

	return (s - buf);
}

static ssize_t Suspend_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x53,0);
	msleep(100);

	return n;
}

static ssize_t Coldboot_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "%d\n",Coldboot_val);

	return (s - buf);
}

static ssize_t Coldboot_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	buffer = atoi(buf);
	Coldboot_val = buffer & 0x0000FFFF;
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x55,Coldboot_val);
	msleep(100);

	return n;
}

static ssize_t Resume_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "%d\n",Resume_val);

	return (s - buf);
}

static ssize_t Resume_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	buffer = atoi(buf);
	Resume_val = buffer & 0x0000FFFF;

	return n;
}

static ssize_t RecoveryMode_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "RecoveryMode");

	return (s - buf);
}

static ssize_t RecoveryMode_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	s16 val;
	buffer = atoi(buf);
	val = buffer & 0x0000FFFF;
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x58,val);
	msleep(100);

	return n;
}

static ssize_t ECflashwrite_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "ECflashwrite");

	return (s - buf);
}

//echo cmd_val16 > ECflashwrite
static ssize_t ECflashwrite_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u16 val16;
	val16 = TransformToWord(buf[2], buf[1]);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client, buf[0], val16);

	return n;
}

char cmd;

static ssize_t ECflashread_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	s32 ret;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,cmd);

	buf[0] = (ret & 0x0000ff00) >> 8;
	buf[1] = ret & 0x000000ff;

	return 2;
}

static ssize_t ECflashread_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	cmd = buf[0];
	return n;
}

#ifdef CONFIG_MACH_ACER_VANGOGH
extern void MicSwitch_int(void)
{
	i2c_smbus_write_word_data(EC_Bat_device->client,0x44,0);
	msleep(100);
}

extern void MicSwitch_ext(void)
{
	i2c_smbus_write_word_data(EC_Bat_device->client,0x44,1);
	msleep(100);
}
#endif

static ssize_t MicSwitch_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "%d\n",MicSwitch_val);

	return (s - buf);
}

/*
 * echo 0 > MicSwitch(front) ,  echo 1 > MicSwitch(back), echo 2 > MicSwitch(normal)
 * echo 3 > MicSwitch(echo cancellation test), echo 4 > MicSwitch(disable echo cancellation)
 */
static ssize_t MicSwitch_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	buffer = atoi(buf);
	MicSwitch_val = buffer & 0x0000FFFF;
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x44,MicSwitch_val);
	msleep(100);

	return n;
}

static ssize_t ThreeGPower_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "%d\n",ThreeGPower_val);

	return (s - buf);
}

//echo 1 > ThreeGPower(poweron) ,  echo 0 > ThreeGPower(poweroff), echo 2 > ThreeGPower(backtoOriginstate)
static ssize_t ThreeGPower_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	buffer = atoi(buf);
	ThreeGPower_val = buffer & 0x0000FFFF;
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x45,ThreeGPower_val);
	msleep(100);

	return n;
}

void enable_ThreeGPower(void)
{
	ThreeGPower_val = 1;
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x45,ThreeGPower_val);
	msleep(100);
}
EXPORT_SYMBOL(enable_ThreeGPower);

static ssize_t ECflashMode_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "%d\n", ECflashMode);
	return (s - buf);
}

/*
 * echo 1 > ECflashMode (ecflash on)
 * echo 0 > ECflashMode (ecflash off)
 */
static ssize_t ECflashMode_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	buffer = atoi(buf);
	ECflashMode = buffer & 0x00000001;
	if(ECflashMode == 0)
	{
		setup_timer(&poll_timer, tegra_battery_poll_timer_func, 0);
		mod_timer(&poll_timer, jiffies + msecs_to_jiffies(batt_status_poll_period));
	}
	else
	{
		del_timer_sync(&poll_timer);
	}

	return n;
}

static ssize_t SkuNumber_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	u8 val8[2] = {0};
	u16 val16;
	s32 val32;

	val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x68);
	val16 = val32 & 0x0000ffff;
	TransformToByte(val16, &val8[0], &val8[1]);
	msleep(10);

	s += sprintf(s, "%02x%02x\n",val8[1],val8[0]);

	return (s - buf);
}

static ssize_t SkuNumber_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;

	val8_0 = TransformCharToByte(buf,2);
	val8_1 = TransformCharToByte(buf,0);

	val16 = TransformToWord(val8_0, val8_1);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x69,val16);
	msleep(10);

	return n;
}

//echo 1 > LEDAndroidOff
static ssize_t LEDAndroidOff_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "LEDAndroidOff");
	return (s - buf);
}

static ssize_t LEDAndroidOff_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x5a,0);
	msleep(100);
	return n;
}

static ssize_t ManufactureDate_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	int i;
	char * s = buf;
	u8 val8[16] = {0};
	u16 val16;
	s32 val32;

	for(i=0;i<=1;i++)
	{
		val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client, 0x6e);
		val16 = val32 & 0x0000ffff;
		TransformToByte(val16, &val8[2*i], &val8[2*i+1]);
		msleep(10);
	}

	s += sprintf(s, "%02x%02x-%02x-%02x\n",val8[3],val8[2],val8[1],val8[0]);

	return (s - buf);
}

static ssize_t ManufactureDate_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;

	val8_0 = TransformCharToByte(buf,6);
	val8_1 = TransformCharToByte(buf,4);
	val16 = TransformToWord(val8_0, val8_1);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x6f,val16);
	msleep(10);

	val8_0 = TransformCharToByte(buf,2);
	val8_1 = TransformCharToByte(buf,0);
	val16 = TransformToWord(val8_0, val8_1);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x6f,val16);
	msleep(10);

	return n;
}

static ssize_t Reset_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "Reset");
	return (s - buf);
}

static ssize_t Reset_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x54,0);
	return n;
}

static ssize_t BatCurrent_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 ret;
	s16 cur;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x03);
	cur = ret & 0x0000ffff;
	msleep(10);
	s += sprintf(s, "%d\n", cur);

	return (s - buf);
}

static ssize_t BatCurrent_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	return n;
}

static ssize_t BoardID_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 ret;
	s16 cur;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x32);
	cur = ret & 0x0000ffff;
	msleep(10);
	s += sprintf(s, "%d\n", cur);

	return (s - buf);
}

static ssize_t BoardID_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	return n;
}

static ssize_t GPSPower_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "%d\n",GPS_val);
	return (s - buf);
}

//echo 0 > GPS(off), echo 1 > GPS(on)
static ssize_t GPSPower_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	buffer = atoi(buf);
	GPS_val = buffer & 0x0000FFFF;
	if(GPS_val == 0)
	{
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x47,0);
	}
	else if(GPS_val == 1)
	{
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x4a,0);
	}
	else
	{
		goto fail;
	}
fail:
	return n;

}

/*
 * Bit0: If sim card plugs in/out, it will be 1
 * Bit1: GPS status, 1 is 3g/gps power on, 0 is power off
 * Bit2: Echo cancellation
 * remaining bits wait to be defined
 */
static ssize_t DeviceStatus_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	char binary[20] = {0};
	int i;
	s32 ret;
	u16 cur;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x48);
	cur = ret & 0x0000ffff;

	for(i=15 ; i>=0; i--)
	{
		binary[i] = cur & 0x1 ? '1' : '0';
		cur >>= 1;
	}

	s += sprintf(s, "%s\n", binary);

	return (s - buf);
}

static ssize_t DeviceStatus_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	u16 val;
	buffer = atoi(buf);
	val = buffer & 0x0000FFFF;
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x49,val);

	return n;
}


static ssize_t PsensorPower_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "%d\n", PsensorPower_val);
	return (s - buf);
}

//echo 0 > PsensorPower(off), echo 1 > PsensorPower(on), echo 2 > PsensorPower(normal control)
static ssize_t PsensorPower_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	buffer = atoi(buf);
	PsensorPower_val = buffer & 0x0000FFFF;
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x4b, PsensorPower_val);
	msleep(100);

	return n;
}

extern void start_stop_psensor(bool on)
{
	if(on)
		PsensorPower_val = 1 & 0xf;
	else
		PsensorPower_val = 0 & 0xf;

	i2c_smbus_write_word_data_retry(EC_Bat_device->client, 0x4b, PsensorPower_val);
	msleep(50);
}

static ssize_t BatSystemofHealth_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 ret;
	u8 health;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x09);
	health = ret & 0x000000ff;
	msleep(10);
	s += sprintf(s, "%d\n", health);

	return (s - buf);
}

static ssize_t BatSystemofHealth_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	return n;
}

static ssize_t BatCycleCount_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 ret;
	s16 cyclecount;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x0b);
	cyclecount = ret & 0x0000ffff;
	msleep(10);
	s += sprintf(s, "%d\n", cyclecount);

	return (s - buf);
}

static ssize_t BatCycleCount_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	return n;
}

static ssize_t AutoLSGain_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	u8 val8[2] = {0};
	u16 val16;
	s32 val32;

	val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x71);
	val16 = val32 & 0x0000ffff;
	TransformToByte(val16, &val8[0], &val8[1]);
	msleep(10);

	s += sprintf(s, "%02x%02x\n",val8[1],val8[0]);

	return (s - buf);
}

static ssize_t AutoLSGain_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;

	val8_0 = TransformCharToByte(buf,2);
	val8_1 = TransformCharToByte(buf,0);

	val16 = TransformToWord(val8_0, val8_1);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x72,val16);
	msleep(10);

	return n;
}

static ssize_t GyroGain_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	int i;
	char * s = buf;
	u8 val8[36] = {0};
	u16 val16;
	s32 val32;

	for(i=0;i<=17;i++)
	{
		val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x73);
		val16 = val32 & 0x0000ffff;
		TransformToByte(val16, &val8[2*i], &val8[2*i+1]);
		msleep(10);
	}

	for(i=35;i>=0;i--)
	{
		s += sprintf(s, "%02x",val8[i]);
	}

	s += sprintf(s, "\n");

	return (s - buf);
}



static ssize_t GyroGain_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16;
	int i;

	for(i=0;i<=17;i++)
	{
		val8_0 = TransformCharToByte(buf,70-4*i);
		val8_1 = TransformCharToByte(buf,68-4*i);
		val16 = TransformToWord(val8_0, val8_1);
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x74,val16);
		msleep(10);
	}

	return n;
}
/*
 * Assume that cat SystemConfig > val
 * val_bit3 is 1 means 3g wake, val_bit3 is 0 means power button wake
 */
static ssize_t SystemConfig_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s32 ret;
	u16 cur;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x4c);
	cur = ret & 0x0000ffff;
	s += sprintf(s, "%d\n", cur);
	msleep(10);

	return (s - buf);
}

/*
 * Assume that cat SystemConfig > val
 * echo val(val_bit0 set 1) > SystemConfig(3G 820 5521)
 * echo val(val_bit1 set 1, val_bit0 set 0) > SystemConfig(3G 770)
 * echo val(val_bit1 set 1, val_bit0 set 1) > SystemConfig(3G LC5740)
 * echo val(val_bit8 set 1) > SystemConfig(CABC on)
 * echo val(val_bit8 set 0) > SystemConfig(CABC off)
 */
static ssize_t SystemConfig_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	u16 val;
	buffer = atoi(buf);
	val = buffer & 0x0000FFFF;

	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x4d,val);
	msleep(10);

	return n;
}



static ssize_t ECRom_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	u16 val16;
	s32 val32;

	val32 = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x78);
	val16 = val32 & 0x0000ffff;
	msleep(10);

	s += sprintf(s, "0x%02x\n",val16);

	return (s - buf);
}

/*
 * echo bank_address > ECRom , for EC ROM read
 * echo bank_address_data > ECRom , for EC ROM write
 */
static ssize_t ECRom_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	u8  val8_0, val8_1;
	u16 val16_bank, val16_address, val16_address_data;

	if(n == 7)    //write data to EC ROM
	{
		val8_0 = TransformCharToByte(buf,4);     //data
		val8_1 = TransformCharToByte(buf,2);     //address
		val16_address_data = TransformToWord(val8_0, val8_1);
		val8_0 = TransformCharToByte(buf,0);     //Rom Bank number
		val16_bank = TransformToWord(val8_0, 0);
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x75,val16_bank);   //write bank
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x76,val16_address_data);   //write address_data
	}
	else if(n == 5)    //write address for EC ROM read
	{
		val8_0 = TransformCharToByte(buf,2);     //address
		val8_1 = TransformCharToByte(buf,0);     //Rom Bank number
		val16_address = TransformToWord(val8_0, 0);
		val16_bank = TransformToWord(val8_1, 0);
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x75,val16_bank);
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x77,val16_address);
	}

	msleep(10);

	return n;
}

#ifdef CONFIG_MACH_ACER_VANGOGH
static ssize_t HomekeyLED_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;
	s += sprintf(s, "HomekeyLED:%d\n",homekeyled_val);
	return (s - buf);
}
/*
 * echo 0 > HomekeyLED (Return to normal control)
 * echo 1 > HomekeyLED (Always on)
 * echo 2 > HomekeyLED (Fast blink)
 * echo 3 > HomekeyLED (Low blink)
 */
static ssize_t HomekeyLED_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int buffer;
	buffer = atoi(buf);
	if (buffer >= 0 && buffer <= 3)
	{
		homekeyled_val = buffer & 0x0000FFFF;
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x46,homekeyled_val);
		msleep(10);
	}
	return n;
}
#endif

debug_attr(EcVer);
debug_attr(UUID);
debug_attr(PowerLED);
debug_attr(ChargeLED);
debug_attr(OriStsLED);
debug_attr(OffLED);
debug_attr(BatCtlEnable);
debug_attr(BatCtlDisable);
debug_attr(BatCapacity);
debug_attr(BatDesignCapacity);
debug_attr(BatStatus);
debug_attr(BTMAC);
debug_attr(WIFIMAC);
debug_attr(ECRead);
debug_attr(ECWrite);
debug_attr(Shutdown);
debug_attr(Suspend);
debug_attr(Coldboot);
debug_attr(Resume);
debug_attr(RecoveryMode);
debug_attr(ECflashwrite);
debug_attr(ECflashread);
debug_attr(MicSwitch);
debug_attr(ThreeGPower);
debug_attr(ECflashMode);
debug_attr(SkuNumber);
debug_attr(LEDAndroidOff);
debug_attr(ManufactureDate);
debug_attr(Reset);
debug_attr(BatCurrent);
debug_attr(BoardID);
debug_attr(GPSPower);
debug_attr(DeviceStatus);
debug_attr(PsensorPower);
debug_attr(BatSystemofHealth);
debug_attr(BatCycleCount);
debug_attr(AutoLSGain);
debug_attr(GyroGain);
debug_attr(SystemConfig);
debug_attr(ECRom);
#ifdef CONFIG_MACH_ACER_VANGOGH
debug_attr(HomekeyLED);
#endif


static struct attribute * g[] = {
	&EcVer_attr.attr,
	&UUID_attr.attr,
	&PowerLED_attr.attr,
	&ChargeLED_attr.attr,
	&OriStsLED_attr.attr,
	&OffLED_attr.attr,
	&BatCtlEnable_attr.attr,
	&BatCtlDisable_attr.attr,
	&BatCapacity_attr.attr,
	&BatDesignCapacity_attr.attr,
	&BatStatus_attr.attr,
	&BTMAC_attr.attr,
	&WIFIMAC_attr.attr,
	&ECRead_attr.attr,
	&ECWrite_attr.attr,
	&Shutdown_attr.attr,
	&Suspend_attr.attr,
	&Coldboot_attr.attr,
	&Resume_attr.attr,
	&ECflashwrite_attr.attr,
	&ECflashread_attr.attr,
	&RecoveryMode_attr.attr,
	&MicSwitch_attr.attr,
	&ThreeGPower_attr.attr,
	&ECflashMode_attr.attr,
	&SkuNumber_attr.attr,
	&LEDAndroidOff_attr.attr,
	&ManufactureDate_attr.attr,
	&Reset_attr.attr,
	&BatCurrent_attr.attr,
	&BoardID_attr.attr,
	&GPSPower_attr.attr,
	&DeviceStatus_attr.attr,
	&PsensorPower_attr.attr,
	&BatSystemofHealth_attr.attr,
	&BatCycleCount_attr.attr,
	&AutoLSGain_attr.attr,
	&GyroGain_attr.attr,
	&SystemConfig_attr.attr,
	&ECRom_attr.attr,
#ifdef CONFIG_MACH_ACER_VANGOGH
	&HomekeyLED_attr.attr,
#endif
	NULL,
};


static struct attribute_group attr_group =
{
	.attrs = g,
};
//SYS_END

void SysShutdown(void )
{
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x52,0);
}
EXPORT_SYMBOL(SysShutdown);

static int is_panic = 0;

static int panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	is_panic = 1;
	return NOTIFY_DONE;
}

static struct notifier_block notifier_blk = {
	.notifier_call = panic_event,
};

void SysRestart(void )
{
	if (!is_panic) {
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x55,1); //cold boot
	}
	else {
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x54,0); //warm boot
	}
}
EXPORT_SYMBOL(SysRestart);

u16 BoardID(void)
{
	s32 ret;
	u16 cur;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x32);
	cur = ret & 0x0000ffff;
	return cur;
}
EXPORT_SYMBOL(BoardID);


int is3Gwakeup(void)
{
	s32 ret;
	u16 cur;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x4c);
	cur = ret & 0x00000008;
	return cur;
}
EXPORT_SYMBOL(is3Gwakeup);

#if defined(CONFIG_LSC_FROM_EC)
struct i2c_client *lsc_from_ec_get_i2c_client(void)
{
	return EC_Bat_device->client;
}
#endif

static void enable_CABC(void)
{
	s32 ret;
	u16 cur;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x4c);
	cur = ret & 0x0000ffff;
	msleep(10);

	cur = cur | (0x1 << 8);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x4d,cur);
	msleep(10);
}
//i2c retry
static s32 i2c_smbus_read_word_data_retry(struct i2c_client *client, u8 command)
{
	int i, ret;

	for(i=0;i<=2;i++)
	{
		ret = i2c_smbus_read_word_data(client,command);

		if(ret >= 0)
		break;

		msleep(10);
		dev_err(&EC_Bat_device->client->dev,
			"%s: i2c_smbus_read_word_data failed, try again\n", __func__);
	}

	if(i == 3)
	{
		dev_err(&EC_Bat_device->client->dev,
			"%s: i2c_smbus_read_word_data failed\n", __func__);
		return -EINVAL;
	}

	return ret;
}

static s32 i2c_smbus_write_word_data_retry(struct i2c_client *client, u8 command, u16 value)
{
	int i, ret;

	for(i=0;i<=2;i++)
	{
		ret = i2c_smbus_write_word_data(client,command,value);

		if(ret == 0)
		break;

		msleep(10);
		dev_err(&EC_Bat_device->client->dev,"%s: i2c_smbus_write_word_data failed, try again\n", __func__);
	}

	if(i == 3)
	{
		dev_err(&EC_Bat_device->client->dev,"%s: i2c_smbus_write_word_data failed\n", __func__);
		return -EINVAL;
	}

	return ret;
}


static int EC_Bat_get_battery_presence_and_health(enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;
	if (psp == POWER_SUPPLY_PROP_PRESENT)
	{
		//read designcapacity to judge present
		ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x08);
		if(ret == 0)//no battery
		{
			dev_err(&EC_Bat_device->client->dev, "%s: No battery, read again\n", __func__);
			msleep(500);
			ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x08);
			if(ret == 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: No battery\n", __func__);
				val->intval = 0;
				return 0;
			}
			else if(ret < 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: i2c read for BatPresent failed\n", __func__);
				return -EINVAL;
			}
		}
		else if (ret < 0)
		{
			dev_err(&EC_Bat_device->client->dev,"%s: i2c read for battery presence failed\n", __func__);
			return -EINVAL;
		}
		val->intval = 1;
	}
	else if (psp == POWER_SUPPLY_PROP_HEALTH)
	{
		//read designcapacity to judge present
		ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x08);
		if(ret == 0)//no battery
		{
			dev_err(&EC_Bat_device->client->dev, "%s: No battery, read again\n", __func__);
			msleep(500);
			ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x08);
			if(ret == 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: No battery\n", __func__);
				val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
				return 0;
			}
			else if(ret < 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: i2c read for BatPresent failed\n",__func__);
				return -EINVAL;
			}
		}
		else if (ret < 0)
		{
			dev_err(&EC_Bat_device->client->dev,"%s: i2c read for battery presence failed\n", __func__);
			return -EINVAL;
		}
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
	}

	return 0;
}

static int EC_Bat_get_battery_property(int reg_offset,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;
	int BatPresent, ACStatus;
	s32 Capacity;
	if (psp == POWER_SUPPLY_PROP_STATUS)
	{
		//read designcapacity to judge present
		//POWER_SUPPLY_PROP_PRESENT is after POWER_SUPPLY_PROP_STATUS
		BatPresent = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x08);

		if(BatPresent >= 0)
		{
			if(BatPresent == 0)//no battery
			{
				dev_err(&EC_Bat_device->client->dev, "%s: No battery, read again\n", __func__);
				msleep(500);
				BatPresent = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x08);
				if(BatPresent == 0)
				{
					dev_err(&EC_Bat_device->client->dev, "%s: No battery\n", __func__);
					val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
					return 0;
				}
				else if(BatPresent < 0)
				{
					dev_err(&EC_Bat_device->client->dev, "%s: i2c read for BatPresent failed\n",__func__);
					return -EINVAL;
				}
			}

			if(acinout == 1 && (CurrCapacity == 99 || CurrCapacity == 100))
			{
				Capacity = CurrCapacity;
			}
			else
			{
				//read capacity
				Capacity = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x00);
			}

			if(Capacity == 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: No battery capacity, read again\n", __func__);
				msleep(500);
				Capacity = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x00);
				if(Capacity == 0)
				{
					dev_err(&EC_Bat_device->client->dev, "%s: No battery capacity\n", __func__);
				}
				else if(Capacity < 0)
				{
					dev_err(&EC_Bat_device->client->dev, "%s: i2c read for capacity failed\n", __func__);
					return -EINVAL;
				}
			}
			else if(Capacity < 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: i2c read for capacity failed\n", __func__);
				return -EINVAL;
			}

			//read ac exist gpio, 0 is exist
			ACStatus = gpio_get_value(gpio);
			if(Capacity < 100)
			{
				if(ACStatus == 1)
				{
					val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
				}
				else
				{
					val->intval = POWER_SUPPLY_STATUS_CHARGING;
				}
			}
			else
			{
				if(ACStatus == 1)
				{
					val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
				}
				else
				{
					val->intval = POWER_SUPPLY_STATUS_FULL;
				}
			}
		}
		else if(BatPresent < 0)
		{
			dev_err(&EC_Bat_device->client->dev, "%s: i2c read for BatPresent failed\n", __func__);
			return -EINVAL;
		}
	}

	else if (psp == POWER_SUPPLY_PROP_TEMP)
	{
		ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x0a);

		if(ret == 0)
		{
			dev_err(&EC_Bat_device->client->dev, "%s: No battery temperature, read again\n", __func__);
			msleep(500);
			ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x0a);
			if(ret == 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: No battery temperature\n", __func__);
				val->intval = 0;
				return 0;
			}
			else if(ret < 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: i2c read for temperature failed\n", __func__);
				return -EINVAL;
			}
		}
		else if(ret < 0)
		{
			dev_err(&EC_Bat_device->client->dev, "%s: i2c read for temperature failed\n", __func__);
			return -EINVAL;
		}

		val->intval = ret - 2731;

	}

	else if (psp == POWER_SUPPLY_PROP_VOLTAGE_NOW)
	{
		ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x01);

		if(ret == 0)
		{
			dev_err(&EC_Bat_device->client->dev, "%s: No battery voltage, read again\n", __func__);
			msleep(500);
			ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x01);
			if(ret == 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: No battery voltage\n", __func__);
			}
			else if(ret < 0)
			{
				dev_err(&EC_Bat_device->client->dev, "%s: i2c read for voltage failed\n", __func__);
				return -EINVAL;
			}
		}
		else if(ret < 0)
		{
			dev_err(&EC_Bat_device->client->dev, "%s: i2c read for voltage failed\n", __func__);
			return -EINVAL;
		}
		val->intval = ret * 1000;
	}

	return 0;
}

static int EC_Bat_get_battery_capacity(union power_supply_propval *val)
{
	s32 ret;
	ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x00);
	if(acinout == 1 && (CurrCapacity == 99 || CurrCapacity == 100))
	{
		val->intval = CurrCapacity;
		return 0;
	}

	if(ret == 0)
	{
		dev_err(&EC_Bat_device->client->dev, "%s: No battery capacity, read again\n", __func__);
		msleep(500);
		ret = i2c_smbus_read_word_data_retry(EC_Bat_device->client,0x00);
		if(ret == 0)
		{
			dev_err(&EC_Bat_device->client->dev, "%s: No battery capacity\n", __func__);
		}
		else if(ret < 0)
		{
			dev_err(&EC_Bat_device->client->dev, "%s: i2c read for capacity failed\n", __func__);
			return -EINVAL;
		}
	}
	else if (ret < 0)
	{
		dev_err(&EC_Bat_device->client->dev, "%s: i2c read for capacity failed\n", __func__);
		return -EINVAL;
	}
	/* EC_Bat spec says that this can be >100 %
	 * even if max value is 100 % */
	CurrCapacity = ret;
	val->intval = ( (ret >= 100) ? 100 : ret);

	if (CurrCapacity <= 19) {
		if (CurrCapacity == 19)
			CurrCapacity = 18;
		else if (CurrCapacity == 18)
			CurrCapacity = 16;
		else if (CurrCapacity == 17)
			CurrCapacity = 15;
		else if (CurrCapacity == 16)
			CurrCapacity = 14;
		else if (CurrCapacity == 15)
			CurrCapacity = 12;
		else if (CurrCapacity == 14)
			CurrCapacity = 11;
		else if (CurrCapacity == 13)
			CurrCapacity = 10;
		else if (CurrCapacity == 12)
			CurrCapacity = 9;
		else if (CurrCapacity == 11)
			CurrCapacity = 8;
		else if (CurrCapacity == 10)
			CurrCapacity = 6;
		else if (CurrCapacity == 9)
			CurrCapacity = 5;
		else if (CurrCapacity == 8)
			CurrCapacity = 4;
		else if (CurrCapacity == 7)
			CurrCapacity = 3;
		else if (CurrCapacity == 6)
			CurrCapacity = 2;
		else if (CurrCapacity == 5)
			CurrCapacity = 1;
		else if (CurrCapacity == 4)
			CurrCapacity = 0;
		else
			printk("-------read capacity error----\n");

                val->intval = CurrCapacity;
        }
	return 0;
}


static int EC_Bat_get_ac_status(union power_supply_propval *val)
{
	int ACStatus;
	//read ac exist gpio, 0 is exist
	ACStatus = gpio_get_value(gpio);
	if(ACStatus)
		val->intval = 0;
	else
		val->intval = 1;
	return 0;
}


static int EC_Bat_get_charge_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	switch (psp)
	{
	case POWER_SUPPLY_PROP_ONLINE:
		if (EC_Bat_get_ac_status(val))
		return -EINVAL;

	break;
		default:
		dev_err(&EC_Bat_device->client->dev,"%s: INVALID property\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static int EC_Bat_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	u8 count;
	switch (psp) {
		case POWER_SUPPLY_PROP_PRESENT:
		case POWER_SUPPLY_PROP_HEALTH:


			if (EC_Bat_get_battery_presence_and_health(psp, val))
				return -EINVAL;

			break;

		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			if (EC_Bat_get_battery_capacity(val))
				return -EINVAL;
			break;
		case POWER_SUPPLY_PROP_STATUS:
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		case POWER_SUPPLY_PROP_TEMP:
			if (EC_Bat_get_battery_property(count, psp, val))
				return -EINVAL;
			break;
		default:
			dev_err(&EC_Bat_device->client->dev,
				"%s: INVALID property\n", __func__);
			return -EINVAL;
	}
	return 0;
}

static void readShutdownID(struct i2c_client *client);

static void readShutdownID(struct i2c_client *client)
{
	s32 val32;
	u16 val16_1;
	u16 val16_2;
	u16 val16_3;
	u16 val16_4;

	i2c_smbus_write_word_data_retry(client,0xF0,0xfbfa);
	msleep(50);
	val32 = i2c_smbus_read_word_data_retry(client,0xF1);
	val16_1 = val32 & 0x000000ff;

	i2c_smbus_write_word_data_retry(client,0xF0,0xfbf9);
	msleep(50);
	val32 = i2c_smbus_read_word_data_retry(client,0xF1);
	val16_2 = val32 & 0x000000ff;

	i2c_smbus_write_word_data_retry(client,0xF0,0xfbf8);
	msleep(50);
	val32 = i2c_smbus_read_word_data_retry(client,0xF1);
	val16_3 = val32 & 0x000000ff;

	i2c_smbus_write_word_data_retry(client,0x75, 0x07);
	i2c_smbus_write_word_data_retry(client,0x77, 0xf4);
	msleep(50);
	val32 = i2c_smbus_read_word_data_retry(client,0x78);
	val16_4 = val32 & 0x000000ff;

	msleep(50);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x75, 0x07);
	i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x76, 0xf4cc);

	printk("Shutdown ID of last shutdown(EC relative, ram) : 0x%x\n", val16_1);
	printk("Shutdown ID of last last shutdown(EC relative, ram) : 0x%x\n", val16_2);
	printk("Shutdown ID of last last last shutdown(EC relative, ram) : 0x%x\n", val16_3);
	printk("Shutdown ID of last shutdown(EC relative, rom) : 0x%x\n", val16_4);

}


static int EC_Bat_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc, err, i;
	int ret = 0;

	gpio = irq_to_gpio(client->irq);//AC detect gpio
	EC_Bat_device = kzalloc(sizeof(*EC_Bat_device), GFP_KERNEL);
	if (!EC_Bat_device)
	{
		goto fail1;
	}

	ret = request_irq(client->irq, ac_interrupt, IRQF_DISABLED | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,client->name,EC_Bat_device);
	if( ret )
	{
		dev_err(&EC_Bat_device->client->dev,"%s: request_irq failed\n", __func__);
		goto fail2;
	}

	EC_Bat_device->client = client;
	i2c_set_clientdata(client, EC_Bat_device);

	for (i = 0; i < ARRAY_SIZE(EC_Bat_supply); i++)
	{
		rc = power_supply_register(&client->dev, &EC_Bat_supply[i]);
		if (rc)
		{
			dev_err(&EC_Bat_device->client->dev,"%s: Failed to register power supply\n", __func__);
			kfree(EC_Bat_device);
			return rc;
		}
	}
	dev_info(&EC_Bat_device->client->dev,
		"%s: battery driver registered\n", client->name);

	batt_status_poll_period = NVBATTERY_POLLING_INTERVAL;
	setup_timer(&poll_timer, tegra_battery_poll_timer_func, 0);
	mod_timer(&poll_timer,
		jiffies + msecs_to_jiffies(batt_status_poll_period));

	//SYS_START
	Ecdebug_kobj = kobject_create_and_add("EcControl", NULL);
	if (Ecdebug_kobj == NULL)
	{
		dev_err(&EC_Bat_device->client->dev,"%s: subsystem_register failed\n", __FUNCTION__);
	}
	err = sysfs_create_group(Ecdebug_kobj, &attr_group);
	if(err)
	{
		dev_err(&EC_Bat_device->client->dev,"%s: sysfs_create_group failed, %d\n", __FUNCTION__, __LINE__);
	}
	//SYS_END

	ret = checkLCM();
	if(ret == 1)
		enable_CABC();

	readShutdownID(client);

	return 0;
fail2:
	free_irq(client->irq, EC_Bat_device);
fail1:
	i2c_set_clientdata(client, NULL);
	kfree(EC_Bat_device);
	EC_Bat_device = NULL;

	return ret;
}

static int EC_Bat_remove(struct i2c_client *client)
{
	struct EC_Bat_device_info *EC_Bat_device = i2c_get_clientdata(client);
	int i;
	del_timer_sync(&poll_timer);
	if(client->irq)
	{
		free_irq(client->irq, EC_Bat_device);
	}

	for (i = 0; i < ARRAY_SIZE(EC_Bat_supply); i++)
	{
		power_supply_unregister(&EC_Bat_supply[i]);
	}
	if (EC_Bat_device) {
		kfree(EC_Bat_device);
		EC_Bat_device = NULL;
	}
	return 0;
}

static int EC_Bat_suspend(struct i2c_client *client,
	pm_message_t state)
{
	struct EC_Bat_device_info *EC_Bat_device = i2c_get_clientdata(client);
	del_timer_sync(&poll_timer);
	if(Resume_val != 0)
		i2c_smbus_write_word_data_retry(EC_Bat_device->client,0x56,Resume_val);
	return 0;
}

/* any smbus transaction will wake up EC_Bat */
static int EC_Bat_resume(struct i2c_client *client)
{
	setup_timer(&poll_timer, tegra_battery_poll_timer_func, 0);
	mod_timer(&poll_timer,jiffies + msecs_to_jiffies(ECBATTERY_RESUME_INTERVAL));
	Resume_val = 0;

	return 0;
}

static const struct i2c_device_id EC_Bat_id[] = {
	{ "EC_Battery", 0 },
	{},
};

static struct i2c_driver EC_Bat_battery_driver = {
	.probe		= EC_Bat_probe,
	.remove 	= EC_Bat_remove,
	.suspend	= EC_Bat_suspend,
	.resume 	= EC_Bat_resume,
	.id_table	= EC_Bat_id,
	.driver = {
		.name	= "EC_Battery",
	},
};

static int __init EC_Bat_battery_init(void)
{
	int ret;

	ret = i2c_add_driver(&EC_Bat_battery_driver);
	if (ret)
		dev_err(&EC_Bat_device->client->dev,
			"%s: i2c_add_driver failed\n", __func__);

	/* Register a call for panic conditions. */
	atomic_notifier_chain_register(&panic_notifier_list,
			&notifier_blk);
	return ret;
}

module_init(EC_Bat_battery_init);
static void __exit EC_Bat_battery_exit(void)
{
	i2c_del_driver(&EC_Bat_battery_driver);
	atomic_notifier_chain_unregister(&panic_notifier_list,
			&notifier_blk);
}
module_exit(EC_Bat_battery_exit);

MODULE_AUTHOR("NVIDIA Graphics Pvt Ltd");
MODULE_DESCRIPTION("BQ20z75 battery monitor driver");
MODULE_LICENSE("GPL");
