#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include <media/yuv5_sensor.h>
#include <media/lsc_from_ec.h>

struct sensor_reg_copy EC_D65_patch_ram_table[LSC_LINE+1];
struct sensor_reg_copy EC_CWF_patch_ram_table[LSC_LINE+1];
int lsc_from_ec_status = 0;

static struct task_struct *lsc_from_ec_task = NULL;

extern struct i2c_client *lsc_from_ec_get_i2c_client(void);

static s32 i2c_smbus_read_word_data_retry_lsc(struct i2c_client *client, u8 command)
{
	int i, ret;

	for (i=0; i<=2; i++) {
		ret = i2c_smbus_read_word_data(client, command);
		if (ret >= 0)
			break;

		msleep(10);
		pr_err("%s: i2c_smbus_read_word_data failed, try again\n", __func__);
	}

	if (i == 3)
		return -EINVAL;

	return ret;
}

static s32 i2c_smbus_write_word_data_retry_lsc(struct i2c_client *client, u8 command, u16 value)
{
	int i, ret;

	for (i=0; i<=2; i++) {
		ret = i2c_smbus_write_word_data(client, command, value);
		if (ret == 0)
			break;

		msleep(10);
		pr_err("%s: i2c_smbus_write_word_data failed, try again\n", __func__);
	}

	if (i == 3)
		return -EINVAL;

	return ret;
}

static void nonOTPM_Lens_Correction_preloading(void)
{
	u16 i=0;
	u16 val16_h_data=0, val16_l_data=0;
	s32 val32 = 0;
	u16 val16_bank = 0x0001;
	u16 val16_address = 0x0000;
	u16 patch_ram_addr = 0x098A;
	u16 mcu_variable_addr = 0x0990;
	u16 patch_val_d65 = 0x1718;
	u16 patch_val_cwf = 0x164C;

	struct i2c_client *client = lsc_from_ec_get_i2c_client();

	pr_info("%s ++\n", __func__);

	// initialize EC_D65_patch_ram_table, EC_CWF_patch_ram_table
	for (i=0; i<LSC_LINE+1; i++) {
		EC_D65_patch_ram_table[i].val = 0xFFFF;
		EC_CWF_patch_ram_table[i].val = 0xFFFF;
	}

	// TODO: workaround: add a delay here to prevent some other EC command which is executed
	//                   just before our commands and has no time delay behind it
	msleep(50);
	for (i=0; i<LSC_LINE-2; i++) {
		if (i%9 == 0) {
			// set patch ram address 0x098A
			u16 addr_offset = i/9 * 0x0010;
			EC_D65_patch_ram_table[i].op = WRITE_REG_DATA16;
			EC_D65_patch_ram_table[i].addr = patch_ram_addr;
			EC_D65_patch_ram_table[i].val = patch_val_d65+addr_offset;

			EC_CWF_patch_ram_table[i].op = WRITE_REG_DATA16;
			EC_CWF_patch_ram_table[i].addr = patch_ram_addr;
			EC_CWF_patch_ram_table[i].val = patch_val_cwf+addr_offset;

			// reset mcu_variable_addr to 0x0990
			mcu_variable_addr = 0x0990;
		} else {
			// set patch ram address 0x0990~0x099E
			// write address for EC ROM read
			i2c_smbus_write_word_data_retry_lsc(client, 0x75, val16_bank);
			i2c_smbus_write_word_data_retry_lsc(client, 0x77, val16_address);
			msleep(20);
			val32 = i2c_smbus_read_word_data_retry_lsc(client, 0x78);
			msleep(20);
			val16_h_data = (u16)(val32 & 0x000000ff);
			val16_address++;

			i2c_smbus_write_word_data_retry_lsc(client, 0x75, val16_bank);
			i2c_smbus_write_word_data_retry_lsc(client, 0x77, val16_address);
			msleep(20);
			val32 = i2c_smbus_read_word_data_retry_lsc(client, 0x78);
			msleep(20);
			val16_l_data = (u16)(val32 & 0x000000ff);
			val16_address++;

			EC_D65_patch_ram_table[i].op = WRITE_REG_DATA16;
			EC_D65_patch_ram_table[i].addr = mcu_variable_addr;
			EC_D65_patch_ram_table[i].val = (val16_h_data<<8) + val16_l_data;

			EC_CWF_patch_ram_table[i].op = WRITE_REG_DATA16;
			EC_CWF_patch_ram_table[i].addr = mcu_variable_addr;
			EC_CWF_patch_ram_table[i].val = (val16_h_data<<8) + val16_l_data;

			mcu_variable_addr += 2;

			if (EC_D65_patch_ram_table[i].val == 0xFFFF) {
				pr_info("%s: EC might not contain LSC data\n", __func__);
				lsc_from_ec_status = -1;
				return;
			}
		}
	}

	// the rest three entries of patch ram table
	EC_D65_patch_ram_table[i].op = WRITE_REG_DATA16;
	EC_D65_patch_ram_table[i].addr = mcu_variable_addr;
	EC_D65_patch_ram_table[i].val = 0x0000;
	EC_CWF_patch_ram_table[i].op = WRITE_REG_DATA16;
	EC_CWF_patch_ram_table[i].addr = mcu_variable_addr;
	EC_CWF_patch_ram_table[i].val = 0x0000;
	i++;
	mcu_variable_addr += 2;

	EC_D65_patch_ram_table[i].op = WRITE_REG_DATA16;
	EC_D65_patch_ram_table[i].addr = mcu_variable_addr;
	EC_D65_patch_ram_table[i].val = 0x0000;
	EC_CWF_patch_ram_table[i].op = WRITE_REG_DATA16;
	EC_CWF_patch_ram_table[i].addr = mcu_variable_addr;
	EC_CWF_patch_ram_table[i].val = 0x0000;
	i++;

	EC_D65_patch_ram_table[i].op = SENSOR_5M_TABLE_END;
	EC_D65_patch_ram_table[i].addr = 0x0000;
	EC_D65_patch_ram_table[i].val = 0x0000;
	EC_CWF_patch_ram_table[i].op = SENSOR_5M_TABLE_END;
	EC_CWF_patch_ram_table[i].addr = 0x0000;
	EC_CWF_patch_ram_table[i].val = 0x0000;

	lsc_from_ec_status = 1;

	for (i=0; i<LSC_LINE+1; i++)
		pr_debug("%s: D65(%03d) addr=%04x val=%04x\n",
		__func__, i, EC_D65_patch_ram_table[i].addr, EC_D65_patch_ram_table[i].val);

	for (i=0; i<LSC_LINE+1; i++)
		pr_debug("%s: CWF(%03d) addr=%04x val=%04x\n",
		__func__, i, EC_CWF_patch_ram_table[i].addr, EC_CWF_patch_ram_table[i].val);

	pr_info("%s --\n", __func__);
}

static int lsc_from_ec_thread (void *num)
{
	pr_info("%s ++\n", __func__);
	nonOTPM_Lens_Correction_preloading();
	pr_info("%s --\n", __func__);

	return 0;
}

static int __init lsc_from_ec_init (void)
{
	int err;

	pr_info("%s\n", __func__);

	lsc_from_ec_task = kthread_create(lsc_from_ec_thread, NULL, "lsc_from_ec_thread");
	if (IS_ERR(lsc_from_ec_task)) {
		pr_err("%s: unable to start kernel thread\n", __func__);
		err = PTR_ERR(lsc_from_ec_task);
		lsc_from_ec_task = NULL;
		return err;
	}

	wake_up_process(lsc_from_ec_task);

	return 0;
}

static void __exit lsc_from_ec_exit (void)
{
	pr_info("%s\n", __func__);

	if (lsc_from_ec_task) {
		kthread_stop(lsc_from_ec_task);
		lsc_from_ec_task = NULL;
	}

	return;
}

module_init(lsc_from_ec_init);
module_exit(lsc_from_ec_exit);

MODULE_DESCRIPTION("Load Lens Correction data from EC driver");
MODULE_LICENSE("GPL");
