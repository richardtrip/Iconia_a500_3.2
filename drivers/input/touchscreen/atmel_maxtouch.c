#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/debugfs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define X_MIN                                 0x00
#define Y_MIN                                 0x00
#define X_MAX                                 0x4FF
#define Y_MAX                                 0x31F
#define MXT_MAX_REPORTED_PRESSURE             255
#define MXT_MAX_TOUCH_SIZE                    255

#define ATMEL_ReadResponseMsg_Noaddress       1
#define ATMEL_ReadResponseMsg                 2
#define ATMEL_ReadSendCMDgetCRCMsg            3
#define ATMEL_HandleTouchMsg                  4

/* Debug levels */
#define DEBUG_DETAIL                          2
#define DEBUG_BASIC                           1
#define DEBUG_ERROR                           0
#define NUM_FINGERS_SUPPORTED                 10
/* #define ConfigChecksum        6160596       T10A_0120.cfg */
#define ConfigChecksum        8159978    /* T10A_0328.cfg */

struct point_data {
        short Status;
        short X;
        short Y;
};

static int debug = DEBUG_ERROR;
static int ConfigError = 0;
static int LastUpdateID = 0;
static bool irq_coming = 0;
static bool ConfigChecksumError = 0;
static u8 checksum[3] = {0};
static u8 FirmwareVer = 0;
static u8 counts = 0;
static u8 i2cfail_esd = 0;
static u8 i2cfail_real = 0;


u16 MsgOBJInf16;            /* Message Processor Byte0 = ID, Byte1 = AddrL, Byte2 = AddrH */
u16 ComOBJInf16;            /* Command Processor */
u16 PowerOBJInf16;          /* Power Configuration */
u16 AcqOBJInf16;            /* Acquisition Configuration */
u16 MTouchOBJInf16;         /* Multiple Touch */
u16 KeyArrayOBJInf16;       /* Key Array */
u16 NoiseOBJInf16;          /* Noise Suppression */
u16 OneTGOBJInf16;          /* One-touch Gesture Processor */
u16 TwoTGOBJInf16;          /* Two-touch Gesture Processor */
u16 SelfTestOBJInf16;       /* Self Test */
u16 CTEOBJInf16;            /* CTE */
u16 PalmSUPOBJInf16;
u16 ComconfigOBJInf16;
u16 UserdataOBJInf16;
u16 DigitizerOBJInf16;
u16 MsgcntOBJInf16;
u16 GripSUPOBJInf16;
u16 DIAGDEBUGOBJInf16;
u16 MaxTableSize;
u16 OBJTableSize;

#define mxt_debug(level, ...) \
	do { \
		if (debug >= (level)) \
			printk(__VA_ARGS__); \
	} while (0)

#define RESERVED_T0                               0
#define RESERVED_T1                               1
#define DEBUG_DELTAS_T2                           2
#define DEBUG_REFERENCES_T3                       3
#define DEBUG_SIGNALS_T4                          4
#define GEN_MESSAGEPROCESSOR_T5                   5
#define GEN_COMMANDPROCESSOR_T6                   6
#define GEN_POWERCONFIG_T7                        7
#define GEN_ACQUISITIONCONFIG_T8                  8
#define TOUCH_MULTITOUCHSCREEN_T9                 9
#define TOUCH_SINGLETOUCHSCREEN_T10               10
#define TOUCH_XSLIDER_T11                         11
#define TOUCH_YSLIDER_T12                         12
#define TOUCH_XWHEEL_T13                          13
#define TOUCH_YWHEEL_T14                          14
#define TOUCH_KEYARRAY_T15                        15
#define PROCG_SIGNALFILTER_T16                    16
#define PROCI_LINEARIZATIONTABLE_T17              17
#define SPT_COMCONFIG_T18                         18
#define SPT_GPIOPWM_T19                           19
#define PROCI_GRIPFACESUPPRESSION_T20             20
#define RESERVED_T21                              21
#define PROCG_NOISESUPPRESSION_T22                22
#define TOUCH_PROXIMITY_T23                       23
#define PROCI_ONETOUCHGESTUREPROCESSOR_T24        24
#define SPT_SELFTEST_T25                          25
#define DEBUG_CTERANGE_T26                        26
#define PROCI_TWOTOUCHGESTUREPROCESSOR_T27        27
#define SPT_CTECONFIG_T28                         28
#define SPT_GPI_T29                               29
#define SPT_GATE_T30                              30
#define TOUCH_KEYSET_T31                          31
#define TOUCH_XSLIDERSET_T32                      32
#define SPARE_T33                                 33
#define SPARE_T34                                 34
#define SPARE_T35                                 35
#define SPARE_T36                                 36
#define DIAG_DEBUG_T37                            37
#define SPT_USERDATA_T38                          38
#define SPARE_T39                                 39
#define PROCI_GRIPSUPPRESSION_T40                 40
#define PROCI_PALMSUPPRESSION_T41                 41
#define SPARE_T42                                 42
#define SPT_DIGITIZER_T43                         43
#define SPT_MESSAGECOUNT_T44                      44

u8 OBJTable[20][6];
u8 MsgOBJInf[3];            /* Message Processor Byte0 = ID, Byte1 = AddrL, Byte2 = AddrH */
u8 ComOBJInf[3];            /* Command Processor */
u8 PowerOBJInf[3];          /* Power Configuration */
u8 AcqOBJInf[3];            /* Acquisition Configuration */
u8 MTouchOBJInf[3];         /* Multiple Touch */
u8 KeyArrayOBJInf[3];       /* Key Array */
u8 NoiseOBJInf[3];          /* Noise Suppression */
u8 OneTGOBJInf[4];          /* One-touch Gesture Processor */
u8 TwoTGOBJInf[3];          /* Two-touch Gesture Processor */
u8 SelfTestOBJInf[3];       /* Self Test */
u8 CTEOBJInf[3];            /* CTE */
u8 PalmSUPOBJInf[3];
u8 ComconfigOBJInf[3];
u8 UserdataOBJInf[3];
u8 DigitizerOBJInf[3];
u8 MsgcntOBJInf[3];
u8 GripSUPOBJInf[3];
u8 DIAGDEBUGOBJInf[3];

/* T10A_0328.cfg */
u8 PowerOBJ[3] = { 50, 10, 25};                                         /* T7 */
u8 AcqOBJ[10] = { 10, 0, 10, 10, 0, 0, 5, 10, 30, 25};                  /* T8 */
u8 MTouchOBJ[34] = { 143,  0,  0, 28,  41,  0,  16, 55,  3, 1,          /* T9 */
                       0, 5, 5, 32,  10,  5,  10,  5, 31, 3, 255, 4,
                       0, 0, 0,  0, 152, 34, 212, 22, 10, 10,  0, 0 };
u8 KeyArrayOBJ[22] = { 1, 24, 41, 4, 1, 0, 0, 255, 1, 0, 0,             /* T15 2 instances */
                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
u8 ComconfigOBJ[2] = { 0, 0};                                           /* T18 */
u8 NoiseOBJ[17] = { 5,  0,  0,  0,  0, 0, 0, 0, 45, 0, 0,               /* T22 */
                   11, 17, 22, 32, 36, 0 };
u8 OneTGOBJ[19] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                 /* T24 */
                    0, 0, 0, 0, 0, 0, 0};
u8 SelfTestOBJ[14] = { 0, 0, 0, 0, 0, 0, 0, 0, 0,                       /* T25 */
                       0, 0, 0, 0, 0};
u8 TwoTGOBJ[7] = { 0, 0, 0, 0, 0, 0, 0};                                /* T27 */
u8 CTEOBJ[6] = { 0, 0, 0, 16, 16, 60};                                  /* T28 */
u8 GripSUPOBJ[5] = { 0, 0, 0, 0, 0};                                    /* T40 */
u8 PalmSUPOBJ[6] = { 0, 0, 0, 0, 0, 0};                                 /* T41 */
u8 DigitizerOBJ[6] = { 0, 0, 0, 0, 0, 0};                               /* T43 */

struct mxt_data
{
	struct i2c_client    *client;
	struct input_dev     *input;
	struct semaphore     sema;
	struct delayed_work  dwork;
	int irq;
	short irq_type;
	struct point_data PointBuf[NUM_FINGERS_SUPPORTED];
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

struct mxt_data  *mxt;
struct mxt_data *mxt_suspend;
static int suspend_resume = 0;
static int ATMEL_Backup(struct mxt_data *mxt);
static int ATMEL_Reset(struct mxt_data *mxt);
static int ATMEL_Deepsleep(struct mxt_data *mxt);
static int ATMEL_Issleep(struct mxt_data *mxt);
static int ATMEL_Resume(struct mxt_data *mxt);
static int ATMEL_IsResume(struct mxt_data *mxt);
static int ATMEL_Calibrate(struct mxt_data *mxt);
static int ATMEL_SyncWithThreadToReadMsg(struct mxt_data *mxt, int boot);
#ifdef CONFIG_HAS_EARLYSUSPEND
void mxt_early_suspend(struct early_suspend *h);
void mxt_late_resume(struct early_suspend *h);
#endif

/* Writes a block of bytes (max 256) to given address in mXT chip. */
static int mxt_write_block(struct i2c_client *client, u16 addr, u16 length, u8 *value)
{
	int i, j;
	struct {
		__le16  le_addr;
		u8      data[256];
	} i2c_block_transfer;
	struct mxt_data *mxt;

	if (length > 256)
		return -EINVAL;

	mxt = i2c_get_clientdata(client);
	for (i = 0; i < length; i++)
		i2c_block_transfer.data[i] = *value++;
	i2c_block_transfer.le_addr = cpu_to_le16(addr);
	i = i2c_master_send(client, (u8 *) &i2c_block_transfer, length + 2);
	if (i == (length + 2)) {
		return length;
	} else {
		for(j=0; j<10; j++) {
			mdelay(10);
			i = i2c_master_send(client, (u8 *) &i2c_block_transfer, length + 2);
			if (i == (length + 2)) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386 i2c write %d time\n", j+2);
				return length;
			}
		}
		mxt_debug(DEBUG_ERROR, "maXTouch1386: i2c write failed\n");
		return -1;
	}
}

static int mxt_read_block(struct i2c_client *client, u16 addr, u16 length, u8 *value)
{
	struct i2c_adapter *adapter = client->adapter;
	struct i2c_msg msg[2];
	__le16 le_addr;
	struct mxt_data *mxt;
	int i;

	mxt = i2c_get_clientdata(client);
	le_addr = cpu_to_le16(addr);
	msg[0].addr  = client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *) &le_addr;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = length;
	msg[1].buf   = (u8 *) value;
	if (i2c_transfer(adapter, msg, 2) == 2) {
		return length;
	} else {
		for(i=0; i<10; i++) {
			mdelay(10);
			if (i2c_transfer(adapter, msg, 2) == 2) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: i2c read %d time\n", i+2);
				return length;
			}
		}
		mxt_debug(DEBUG_ERROR, "maXTouch1386: i2c read failed\n");
		return -1;
	}
}

static int mxt_read_block_onetime(struct i2c_client *client, u16 addr, u16 length, u8 *value)
{
	struct i2c_adapter *adapter = client->adapter;
	struct i2c_msg msg[2];
	__le16  le_addr;
	struct mxt_data *mxt;

	mxt = i2c_get_clientdata(client);
	le_addr = cpu_to_le16(addr);
	msg[0].addr  = client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *) &le_addr;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = length;
	msg[1].buf   = (u8 *) value;
	if(i2c_transfer(adapter, msg, 2) == 2) {
		return length;
	} else {
		mdelay(10);
		mxt_debug(DEBUG_ERROR, "maXTouch1386: i2c read failed\n");
		return -1;
	}
}

static int mxt_read_block_wo_addr(struct i2c_client *client, u16 length, u8 *value)
{
	int i;

	if (i2c_master_recv(client, value, length) == length) {
		return length;
	} else {
		for(i=0; i<10; i++){
			mdelay(10);
			if(i2c_master_recv(client, value, length) == length) {
				mxt_debug(DEBUG_ERROR, "maXTOuch1386: i2c read %d time\n", i+2);
				return length;
			}
		}
		mxt_debug(DEBUG_ERROR, "maXTouch1386: i2c read failed\n");
		return -1;
	}
}

static void CalculateAddr16bits(u8 hbyte_input, u8 lbyte_input, u16 *output, u32 val)
{
	u16 temp;

	temp = hbyte_input;
	temp = (temp << 8) + lbyte_input;
	temp = temp + val;
	*output = temp;
}

static void mxt_worker(struct work_struct *work)
{
	struct mxt_data *mxt;
	u8 buffer[8] = {0};
	u16 Buf = 0;
	short ContactID = 0;
	bool first_touch = 0;
	int i;

	mxt = container_of(work, struct mxt_data, dwork.work);
	disable_irq(mxt->irq);
	mxt_debug(DEBUG_DETAIL, "mxt_worker, and irq_type is %d\n",mxt->irq_type);

	if (mxt->irq_type == ATMEL_ReadResponseMsg_Noaddress) {
		counts++;      /* identify touch source only for first message reading */
		if(counts == 1) {
			for(i=0;i<=1;i++) {    /* identify two touch sources two time */
				if(mxt_read_block_wo_addr(mxt->client, 8, buffer) < 0) {
					mxt_debug(DEBUG_ERROR, "maXTouch1386: identify CANDO source failed, start to identify Sintek source\n");
					mxt->client->addr = 0x4d;
					if (mxt_read_block_wo_addr(mxt->client, 8, buffer) < 0) {
						if (i == 1) {    /* i == 1 means this is second round */
							mxt_debug(DEBUG_ERROR, "maXTouch1386: identify two sources failed\n");
							up(&mxt->sema);
							goto fail;
						} else {
							mxt_debug(DEBUG_ERROR, "maXTouch1386: identify two sources failed, try again\n");
							mxt->client->addr = 0x4c;
							msleep(100);
						}
					} else {
						mxt_debug(DEBUG_ERROR, "maXTouch1386: It is Sintek source\n");
						break;
					}
				} else {
					mxt_debug(DEBUG_ERROR, "maXTouch1386: It is CANDO source\n");
					break;
				}
			}
		} else {
			if (mxt_read_block_wo_addr(mxt->client, 8, buffer) < 0) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_read_block_wo_addr failed\n");
				i2cfail_real = 1;
				up(&mxt->sema);
				goto fail;
			}
		}

		i2cfail_real = 0;
		mxt_debug(DEBUG_DETAIL, "print read message\n");
		if (debug == DEBUG_DETAIL) {
			for(i=0;i<=7;i++)
				mxt_debug(DEBUG_DETAIL, "buffer is 0x%x\n",buffer[i]);
		}
		if (buffer[0] == 0x01) {
			if (buffer[1] != 0x00) {
				if ((buffer[1] & 0x08)==0x08) {
					ConfigError = 1;
					mxt_debug(DEBUG_DETAIL, "release sema\n");
					up(&mxt->sema);
					goto still_disable_irq;
				} else if((buffer[1] & 0x40)==0x40) {
					ConfigError = 0;
					mxt_debug(DEBUG_ERROR, "maXTouch1386: OFL overflow occurs\n");
					up(&mxt->sema);
					goto still_disable_irq;
				} else {
					ConfigError = 0;
				}
			} else {
				ConfigError = 0;
				mxt_debug(DEBUG_DETAIL, "release sema\n");
				up(&mxt->sema);
				goto still_disable_irq;
			}
		}
	} else if (mxt->irq_type == ATMEL_ReadResponseMsg) {
		if (mxt_read_block(mxt->client, MsgOBJInf16, 8, buffer) < 0) {
			mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_read_block failed\n");
			i2cfail_real = 1;
			up(&mxt->sema);
			goto fail;
		}
		i2cfail_real = 0;
		mxt_debug(DEBUG_DETAIL, "print read message\n");
		if (debug == DEBUG_DETAIL) {
			for(i=0;i<=7;i++)
				mxt_debug(DEBUG_DETAIL, "buffer is 0x%x\n",buffer[i]);
		}
		if (buffer[0] == 0x01) {
			if (buffer[1] != 0x00) {
				if ((buffer[1] & 0x08)==0x08) {
					ConfigError = 1;
					mxt_debug(DEBUG_DETAIL, "release sema\n");
					up(&mxt->sema);
					goto still_disable_irq;
				 } else if((buffer[1] & 0x40)==0x40) {
					ConfigError = 0;
					mxt_debug(DEBUG_ERROR, "maXTouch1386: OFL overflow occurs\n");
					up(&mxt->sema);
					goto still_disable_irq;
				} else {
					ConfigError = 0;
				}
			} else {
				ConfigError = 0;
				mxt_debug(DEBUG_DETAIL, "release sema\n");
				up(&mxt->sema);
				goto still_disable_irq;
			}
		}
	} else if (mxt->irq_type == ATMEL_ReadSendCMDgetCRCMsg) {
		if (mxt_read_block(mxt->client, MsgOBJInf16, 8, buffer) < 0) {
			mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_read_block failed\n");
			i2cfail_real = 1;
			up(&mxt->sema);
			goto fail;
		}
		i2cfail_real = 0;
		mxt_debug(DEBUG_DETAIL, "print read message\n");
		if (debug == DEBUG_DETAIL) {
			for(i=0;i<=7;i++)
				mxt_debug(DEBUG_DETAIL, "buffer is 0x%x\n",buffer[i]);
		}
		if (buffer[0] == 0x01) {
			checksum[0] = buffer[2];
			checksum[1] = buffer[3];
			checksum[2] = buffer[4];
		} else if (buffer[0] == 0x1b) {
			mxt_debug(DEBUG_DETAIL, "release sema\n");
			up(&mxt->sema);
			goto still_disable_irq;
		}
	} else if (mxt->irq_type == ATMEL_HandleTouchMsg) {
		if (mxt_read_block_onetime(mxt->client, MsgOBJInf16, 8, buffer) < 0) {
			i2cfail_esd = 1;
			mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_read_block failed, try again\n");
			if (mxt_read_block(mxt->client, MsgOBJInf16, 8, buffer) < 0) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_read_block fail\n");
				goto fail;
			}
		}
		if(i2cfail_esd == 1) {
			/* ESD recovery */
			for(i=0; i<NUM_FINGERS_SUPPORTED; i++) {
				mxt->PointBuf[i].X = 0;
				mxt->PointBuf[i].Y = 0;
				mxt->PointBuf[i].Status = -1;
			}
			i2cfail_esd = 0;
		}
		mxt_debug(DEBUG_DETAIL, "buffer[0] is 0x%x\n", buffer[0]);
		if (buffer[0] >= 2 && buffer[0] <= NUM_FINGERS_SUPPORTED+1) {
			mxt_debug(DEBUG_DETAIL, "report id is 0x%x\n",buffer[0]);
			ContactID = buffer[0] - 2 ; /* touch id from firmware begins from 2 */
			Buf = buffer[2];
			Buf = (Buf << 4) | (buffer[4] >> 4);
			mxt->PointBuf[ContactID].X = Buf;
			mxt_debug(DEBUG_DETAIL, "X hbyte is 0x%x xy lbyte is 0x%x\n",buffer[2],buffer[4]);
			mxt_debug(DEBUG_DETAIL, "X is 0x%x\n",Buf);
			Buf = buffer[3];
			Buf = (Buf << 4) | (buffer[4] & 0x0F);
			Buf = Buf >> 2;              /* y resolution is 800, use 10-bit format */
			mxt->PointBuf[ContactID].Y = Buf;
			mxt_debug(DEBUG_DETAIL, "Y hbyte is 0x%x xy lbyte is 0x%x\n",buffer[3],buffer[4]);
			mxt_debug(DEBUG_DETAIL, "Y is 0x%x\n",Buf);
			mxt_debug(DEBUG_DETAIL, "SIZE is 0x%x\n",buffer[5]);
			mxt_debug(DEBUG_DETAIL, " ==== message buffer ====\n");
			for(i=0;i<=7;i++)
				mxt_debug(DEBUG_DETAIL, "0x%x\n",buffer[i]);

			if(mxt->PointBuf[ContactID].Status <= 0)
				first_touch = 1;
			else
				first_touch = 0;

			if ((buffer[1] & 0x20)==0x20) {
				mxt->PointBuf[ContactID].Status = 0;
				mxt_debug(DEBUG_DETAIL, "FingerRelease!!\n");
			} else if ((buffer[1] & 0x80)==0x80) {
				mxt->PointBuf[ContactID].Status = 1;
				mxt_debug(DEBUG_DETAIL, "FingerTouch!!\n");
			} else if ((buffer[1] & 0x02)==0x02) {
				ContactID = 255;
				mxt_debug(DEBUG_DETAIL, "PalmSuppresion!!\n");
			} else if(buffer[1] == 0x00) {
				ContactID = 255;
				mxt_debug(DEBUG_DETAIL, "this point is not touch or release\n");
			}
		} else {
			mxt_debug(DEBUG_DETAIL, "not a member of touch point\n");
			mxt_debug(DEBUG_DETAIL, "print all read message\n");
			if (debug == DEBUG_DETAIL) {
				for(i = 0; i <=7; i++)
				mxt_debug(DEBUG_DETAIL, "peter ==== message is 0x%x\n",buffer[i]);
			}
			ContactID = 255 ;
		}
		if (ContactID == 255)
			goto next_irq;
		mxt_debug(DEBUG_BASIC, "Get Point[%d] Update: Status=%d X=%d Y=%d\n",
		ContactID, mxt->PointBuf[ContactID].Status, mxt->PointBuf[ContactID].X, mxt->PointBuf[ContactID].Y);
		/* Send point report to Android */
		if ((mxt->PointBuf[ContactID].Status == 0) || (ContactID <= LastUpdateID) || first_touch) {
			for(i=0; i<NUM_FINGERS_SUPPORTED; i++) {
				if (mxt->PointBuf[i].Status >= 0) {
					mxt_debug(DEBUG_BASIC,
						"Report Point[%d] Update: Status=%d X=%d Y=%d\n",
						i, mxt->PointBuf[i].Status, mxt->PointBuf[i].X, mxt->PointBuf[i].Y);

					input_report_abs(mxt->input, ABS_MT_TRACKING_ID, i);
					input_report_abs(mxt->input, ABS_MT_TOUCH_MAJOR, mxt->PointBuf[i].Status);
					input_report_abs(mxt->input, ABS_MT_POSITION_X, mxt->PointBuf[i].X);
					input_report_abs(mxt->input, ABS_MT_POSITION_Y, mxt->PointBuf[i].Y);

					input_mt_sync(mxt->input);

					if (mxt->PointBuf[i].Status == 0)
						mxt->PointBuf[i].Status--;
				}
			}
			input_sync(mxt->input);
		}
		LastUpdateID = ContactID;
	}

next_irq:
fail:
	enable_irq(mxt->irq);
still_disable_irq:
	return;
}

static irqreturn_t mxt_irq_handler(int irq, void *_mxt)
{
	struct mxt_data *mxt = _mxt;

	mxt_debug(DEBUG_DETAIL, "\n mxt_irq_handler\n");
	irq_coming = 1;

	cancel_delayed_work(&mxt->dwork);
	schedule_delayed_work(&mxt->dwork, 0);

	return IRQ_HANDLED;
}

/* SYSFS_START */
char hbyte, lbyte, val;
int rlen;

int myatoi(const char *a)
{
	int s = 0;

	while(*a >= '0' && *a <= '9')
		s = (s << 3) + (s << 1) + *a++ - '0';
	return s;
}

static ssize_t hbyte_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char * s = buf;

	s += sprintf(s, "0x%x\n",hbyte);
	return (s - buf);
}

static ssize_t hbyte_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int ibuf;

	ibuf = myatoi(buf);
	hbyte = ibuf & 0x000000ff;
	mxt_debug(DEBUG_ERROR, "maXTouch1386: hbyte is 0x%x\n", hbyte);
	return n;
}

static ssize_t lbyte_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char *s = buf;

	s += sprintf(s, "0x%x\n",lbyte);
	return (s - buf);
}

static ssize_t lbyte_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int ibuf;

	ibuf = myatoi(buf);
	lbyte = ibuf & 0x000000ff;
	mxt_debug(DEBUG_ERROR, "maXTouch1386: lbyte is 0x%x\n", lbyte);
	return n;
}

static ssize_t rlen_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char *s = buf;

	s += sprintf(s, "0x%x\n",val);
	return (s - buf);
}

static ssize_t rlen_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	rlen = myatoi(buf);
	mxt_debug(DEBUG_ERROR, "maXTouch1386: rlen is %d\n", rlen);
	return n;
}

static ssize_t val_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char *s = buf;
	s += sprintf(s, "%c\n",val);
	return (s - buf);
}

static ssize_t val_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int ibuf,i;
	char wdatabuf[1] = {0};
	char *rdatabuf;
	u16 addr16;

	rdatabuf = kzalloc(sizeof(char)*rlen, GFP_KERNEL);
	ibuf = myatoi(buf);
	val = ibuf & 0x000000ff;
	wdatabuf[0] = val;

	mxt_debug(DEBUG_ERROR, "maXTouch1386: val is 0x%x\n", val);
	mxt_debug(DEBUG_ERROR, "maXTouch1386: hbyte is 0x%x, lbyte is 0x%x\n", hbyte, lbyte);

	CalculateAddr16bits(hbyte, lbyte, &addr16, 0);

	if (!strncmp(buf,"r", 1)) {
		mxt_read_block(mxt->client, addr16, rlen, rdatabuf);
		for(i=0;i<rlen;i++)
			mxt_debug(DEBUG_ERROR, "maXTouch1386: rdatabuf is 0x%x\n", rdatabuf[i]);
	} else if (!strncmp(buf,"b", 1)) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: backup\n");
		ATMEL_Backup(mxt);
	} else if (!strncmp(buf,"t", 1)) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: reset\n");
		ATMEL_Reset(mxt);
	} else if(!strncmp(buf,"s", 1)) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: sleep\n");
		ATMEL_Deepsleep(mxt);
	} else if(!strncmp(buf,"m", 1)) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: resume\n");
		ATMEL_Resume(mxt);
	} else {
		mxt_write_block(mxt->client, addr16, 1, wdatabuf);
	}

	kfree(rdatabuf);

	return n;
}

static ssize_t debugmsg_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char *s = buf;

	s += sprintf(s, "%d\n",debug);
	return (s - buf);
}

static ssize_t debugmsg_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	debug = myatoi(buf);
	printk("debug is %d\n", debug);
	return n;
}

static ssize_t sensitivity_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	u8 sens_buffer[8] = {0};
	if (mxt_read_block(mxt->client, MTouchOBJInf16, 8, sens_buffer) < 0)
		return sprintf(buf, "fail\n");
	return sprintf(buf, "0x%x\n",sens_buffer[7]);
}

static ssize_t sensitivity_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int sens = myatoi(buf);
	u8 sen_value[1] = {sens};
	u16 sens_addr16;
	CalculateAddr16bits(MTouchOBJInf[2], MTouchOBJInf[1], &sens_addr16, 7);
	if(mxt_write_block(mxt->client, sens_addr16, 1, sen_value) < 0)
		mxt_debug(DEBUG_ERROR, "sensitivity_store fail\n");
	if (ATMEL_Backup(mxt) < 0)
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_Backup failed\n");
	return n;
}

static ssize_t filter_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	u8 filter[14] = {0};
	if (mxt_read_block(mxt->client, MTouchOBJInf16, 14, filter) < 0)
		return sprintf(buf, "fail\n");
	return sprintf(buf, "filter1: 0x%x\nfilter2: 0x%x\nfilter3: 0x%x\n", filter[11], filter[12], filter[13]);
}

static ssize_t filter_store(struct kobject *kobj, struct kobj_attribute *attr, const char * buf, size_t n)
{
	int XT9_DECISION = myatoi(buf);
	u8 filter_value[3] = {0};
	u16 addr;

	CalculateAddr16bits(MTouchOBJInf[2], MTouchOBJInf[1], &addr, 11);

	if(XT9_DECISION){
		filter_value[0] = 0;
		filter_value[1] = 1;
		filter_value[2] = 78;
		if(mxt_write_block(mxt->client, addr, 3, filter_value) < 0)
			mxt_debug(DEBUG_ERROR, "filter_store fail\n");
	} else {
		filter_value[0] = 5;
		filter_value[1] = 5;
		filter_value[2] = 32;
		if(mxt_write_block(mxt->client, addr, 3, filter_value) < 0)
			mxt_debug(DEBUG_ERROR, "filter_store fail\n");
	}
	return n;
}

static ssize_t FirmwareVersion_show(struct kobject *kobj, struct kobj_attribute *attr, char * buf)
{
	char *s = buf;
	int version = 0;

	version = FirmwareVer;
	s += sprintf(s, "%x\n", version);
	return (s - buf);
}

static struct kobject *touchdebug_kobj;

#define debug_attr(_name) \
	static struct kobj_attribute _name##_attr = { \
	.attr = { \
	.name = __stringify(_name), \
	.mode = 0644, \
	}, \
	.show = _name##_show, \
	.store = _name##_store, \
	}

static struct kobj_attribute FirmwareVersion_attr = { \
	.attr = { \
	.name = __stringify(FirmwareVersion), \
	.mode = 0644, \
	}, \
	.show = FirmwareVersion_show, \
};

debug_attr(hbyte);
debug_attr(lbyte);
debug_attr(rlen);
debug_attr(val);
debug_attr(debugmsg);
debug_attr(sensitivity);
debug_attr(filter);

static struct attribute * g[] = {
	&hbyte_attr.attr,
	&lbyte_attr.attr,
	&rlen_attr.attr,
	&val_attr.attr,
	&debugmsg_attr.attr,
	&sensitivity_attr.attr,
	&filter_attr.attr,
	&FirmwareVersion_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};
/* SYSFS_END */

static int ATMEL_ReadIDInfo(struct mxt_data *mxt)
{
	u8 buffer[8] = {0};
	int i;

	if (mxt_read_block(mxt->client, 0, 7, buffer) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_read_block fail\n");
		return -1;
	}
	if (debug == DEBUG_DETAIL) {
		for(i=0;i<=6;i++)
			mxt_debug(DEBUG_DETAIL, "ID info are 0x%x\n",buffer[i]);
	}
	FirmwareVer = buffer[2];
	MaxTableSize = (buffer[6] * 6) + 10;
	OBJTableSize = buffer[6];

	return 0;
}

static void ATMEL_CalOBJ_ID_Addr(u8 Type, u8 *Table)
{
	u8 i, ID, z;
	ID = 1;

	for(i = 0; i < MaxTableSize; i++) {
		if (OBJTable[i][0] == Type) {
			if (Type == TOUCH_MULTITOUCHSCREEN_T9) {
				Table[1] = OBJTable[i][1];
				Table[2] = OBJTable[i][2];
				for(z = 0; z < NUM_FINGERS_SUPPORTED; z++) {
					ID++;
				}
			} else if (Type == PROCI_ONETOUCHGESTUREPROCESSOR_T24) {
				Table[2] = OBJTable[i][1];
				Table[3] = OBJTable[i][2];
				Table[0] = ID;
				ID++;
				Table[1] = ID;
			} else {
				if (OBJTable[i][5] != 0)
					Table[0] = ID;
				else
					Table[0] = 0x00;
					Table[1] = OBJTable[i][1];
					Table[2] = OBJTable[i][2];
			}
			return;
		} else {
			ID += (OBJTable[i][4]+1) * OBJTable[i][5];
		}
	}
}

static u32 ATMEL_CRCSoft24(u32 crc, u8 FirstByte, u8 SecondByte)
{
	u32 crcPoly;
	u32 Result;
	u16 WData;

	crcPoly = 0x80001b;

	WData = (u16) ((u16)(SecondByte << 8) | FirstByte);

	Result = ((crc << 1) ^ ((u32) WData));
	if (Result & 0x1000000) {
		Result ^= crcPoly;
	}

	return Result;
}

static int ATMEL_CheckOBJTableCRC(struct mxt_data *mxt)
{
  u8 *buffer;
	u8 i, z, Value;
	u32 Cal_crc = 0, InternalCRC;
	buffer = kzalloc(sizeof(u8)*MaxTableSize + 1, GFP_KERNEL);

	/* read all table */
	if (mxt_read_block(mxt->client, 0, MaxTableSize, buffer) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_read_block failed\n");
		return -1;
	}
	i = 0;
	while(1) {
		if ((i + 2) > (MaxTableSize - 3)) {
			Cal_crc = ATMEL_CRCSoft24(Cal_crc, buffer[i], 0);
		} else {
			Cal_crc = ATMEL_CRCSoft24(Cal_crc, buffer[i], buffer[i + 1]);
		}
			i = i + 2;
		if ((i == (MaxTableSize - 3)) || (i > (MaxTableSize - 3)))
			break;
	}

	InternalCRC = buffer[MaxTableSize - 1];
	InternalCRC = (InternalCRC << 8) + buffer[MaxTableSize - 2];
	InternalCRC = (InternalCRC << 8) + buffer[MaxTableSize - 3];

	if ((Cal_crc & 0x00ffffff) == InternalCRC) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: Check OBJ Table CRC is ok....\n");
	} else {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: Check OBJ Table CRC is fail....\n");
		return -1;
	}

	z = 0;
	Value = 7;
	mxt_debug(DEBUG_DETAIL, "OBJTable\n");

	while(1){
		for(i = 0; i < 6; i++) {
			OBJTable[z][i] = buffer[Value];
			mxt_debug(DEBUG_DETAIL, "0x%x ",buffer[Value]);
			Value++;
		}
		mxt_debug(DEBUG_DETAIL, "\n");
		z++;
		if (z == OBJTableSize) {
			break;
		}
	}

	ATMEL_CalOBJ_ID_Addr(GEN_MESSAGEPROCESSOR_T5, MsgOBJInf);
	CalculateAddr16bits(MsgOBJInf[2], MsgOBJInf[1], &MsgOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(GEN_COMMANDPROCESSOR_T6, ComOBJInf);
	CalculateAddr16bits(ComOBJInf[2], ComOBJInf[1], &ComOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(GEN_POWERCONFIG_T7, PowerOBJInf);
	CalculateAddr16bits(PowerOBJInf[2], PowerOBJInf[1], &PowerOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(GEN_ACQUISITIONCONFIG_T8, AcqOBJInf);
	CalculateAddr16bits(AcqOBJInf[2], AcqOBJInf[1], &AcqOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(TOUCH_MULTITOUCHSCREEN_T9, MTouchOBJInf);
	CalculateAddr16bits(MTouchOBJInf[2], MTouchOBJInf[1], &MTouchOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(TOUCH_KEYARRAY_T15, KeyArrayOBJInf);
	CalculateAddr16bits(KeyArrayOBJInf[2], KeyArrayOBJInf[1], &KeyArrayOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(PROCG_NOISESUPPRESSION_T22, NoiseOBJInf);
	CalculateAddr16bits(NoiseOBJInf[2], NoiseOBJInf[1], &NoiseOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(PROCI_ONETOUCHGESTUREPROCESSOR_T24, OneTGOBJInf);
	CalculateAddr16bits(OneTGOBJInf[3], OneTGOBJInf[2], &OneTGOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(PROCI_TWOTOUCHGESTUREPROCESSOR_T27, TwoTGOBJInf);
	CalculateAddr16bits(TwoTGOBJInf[2], TwoTGOBJInf[1], &TwoTGOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(SPT_SELFTEST_T25, SelfTestOBJInf);
	CalculateAddr16bits(SelfTestOBJInf[2], SelfTestOBJInf[1], &SelfTestOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(SPT_CTECONFIG_T28, CTEOBJInf);
	CalculateAddr16bits(CTEOBJInf[2], CTEOBJInf[1], &CTEOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(PROCI_PALMSUPPRESSION_T41, PalmSUPOBJInf);
	CalculateAddr16bits(PalmSUPOBJInf[2], PalmSUPOBJInf[1], &PalmSUPOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(SPT_COMCONFIG_T18, ComconfigOBJInf);
	CalculateAddr16bits(ComconfigOBJInf[2], ComconfigOBJInf[1], &ComconfigOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(SPT_USERDATA_T38, UserdataOBJInf);
	CalculateAddr16bits(UserdataOBJInf[2], UserdataOBJInf[1], &UserdataOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(SPT_DIGITIZER_T43, DigitizerOBJInf);
	CalculateAddr16bits(DigitizerOBJInf[2], DigitizerOBJInf[1], &DigitizerOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(SPT_MESSAGECOUNT_T44, MsgcntOBJInf);
	CalculateAddr16bits(MsgcntOBJInf[2], MsgcntOBJInf[1], &MsgcntOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(PROCI_GRIPSUPPRESSION_T40, GripSUPOBJInf);
	CalculateAddr16bits(GripSUPOBJInf[2], GripSUPOBJInf[1], &GripSUPOBJInf16, 0);

	ATMEL_CalOBJ_ID_Addr(DIAG_DEBUG_T37, DIAGDEBUGOBJInf);
	CalculateAddr16bits(DIAGDEBUGOBJInf[2], DIAGDEBUGOBJInf[1], &DIAGDEBUGOBJInf16, 0);

	if (debug == DEBUG_DETAIL) {
		mxt_debug(DEBUG_DETAIL, "Seperate Table\n");
		mxt_debug(DEBUG_DETAIL, "MsgOBJInf[2] is 0x%x, MsgOBJInf[1] is 0x%x, MsgOBJInf16 is 0x%x\n",
				MsgOBJInf[2], MsgOBJInf[1],MsgOBJInf16);

		mxt_debug(DEBUG_DETAIL, "MsgOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",MsgOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "ComOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",ComOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "PowerOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",PowerOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "AcqOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",AcqOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "MTouchOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",MTouchOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "KeyArrayOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",KeyArrayOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "NoiseOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",NoiseOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "OneTGOBJInf\n");
		for(i=0;i<=3;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",OneTGOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "TwoTGOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",TwoTGOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "SelfTestOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",SelfTestOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "CTEOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",CTEOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "PalmSUPOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",PalmSUPOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "ComconfigOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",ComconfigOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "UserdataOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",UserdataOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "DigitizerOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",DigitizerOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "MsgcntOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",MsgcntOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "GripSUPOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",GripSUPOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");

		mxt_debug(DEBUG_DETAIL, "DIAGDEBUGOBJInf\n");
		for(i=0;i<=2;i++)
			mxt_debug(DEBUG_DETAIL, "0x%x ",DIAGDEBUGOBJInf[i]);
		mxt_debug(DEBUG_DETAIL, "\n");
	}
	kfree(buffer);

	return 0;
}

static int ATMEL_SetCTE(struct mxt_data *mxt)
{

	mxt_debug(DEBUG_DETAIL, "maXTouch1386: ATMEL_SetCTE\n");

	if (mxt_write_block(mxt->client, CTEOBJInf16, 6, CTEOBJ) < 0)
		return -1;

	return 0;
}

static int ATMEL_Backup(struct mxt_data *mxt)
{
	u8 val[1] = {85};
	u16 addr16;

	mxt_debug(DEBUG_DETAIL, "maXTouch1386: ATMEL_Backup\n");

	CalculateAddr16bits(ComOBJInf[2],ComOBJInf[1], &addr16, 1);
	if(mxt_write_block(mxt->client, addr16, 1, val) < 0)
		return -1;

	return 0;
}

static int ATMEL_Reset(struct mxt_data *mxt)
{
	u8 val[1] = {1};

	mxt_debug(DEBUG_DETAIL, "maXTouch1386: ATMEL_Reset\n");

	if(mxt_write_block(mxt->client, ComOBJInf16, 1, val) < 0)
		return -1;

	return 0;
}

static int ATMEL_Calibrate(struct mxt_data *mxt)
{
	u8 val[1] = {1};
	u16 addr16;

	mxt_debug(DEBUG_DETAIL, "maXTouch1386: ATMEL_Calibrate\n");

	CalculateAddr16bits(ComOBJInf[2],ComOBJInf[1], &addr16, 2);
	if(mxt_write_block(mxt->client, addr16, 1, val) < 0)
		return -1;

	return 0;
}

static int ATMEL_SendCMDgetCRC(struct mxt_data *mxt)
{
	u8 val[1] = {1};
	u16 addr16;
	u32 checksum32;

	mxt_debug(DEBUG_DETAIL, "maXTouch1386: ATMEL_SendCMDgetCRC\n");

	CalculateAddr16bits(ComOBJInf[2],ComOBJInf[1], &addr16, 3);
	if(mxt_write_block(mxt->client, addr16, 1, val) < 0)
		return -1;

	mxt->irq_type = ATMEL_ReadSendCMDgetCRCMsg;
	if (ATMEL_SyncWithThreadToReadMsg(mxt,0) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_SyncWithThreadToReadMsg failed\n");
		return -1;
	}

	checksum32 = checksum[2];
	checksum32 = (checksum32 << 8) + checksum[1];
	checksum32 = (checksum32 << 8) + checksum[0];

	if (checksum32 != ConfigChecksum) {
		mxt_debug(DEBUG_ERROR,
				"maXTouch1386: ConfigChecksumError is set to 1, checksum32 now is %d -> should be %d\n",
				checksum32, ConfigChecksum);
		ConfigChecksumError = 1;
	} else {
		mxt_debug(DEBUG_ERROR,
				"maXTouch1386: There is no ConfigChecksumError, checksum32 now is %d -> should be %d\n",
				checksum32, ConfigChecksum);
		ConfigChecksumError = 0;
	}

	return 0;
}

static int ATMEL_WriteConfig(struct mxt_data *mxt)
{
	mxt_debug(DEBUG_DETAIL, "maXTouch1386: ATMEL_WriteConfig\n");

	if(mxt_write_block(mxt->client, PowerOBJInf16, 3, PowerOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, AcqOBJInf16, 10, AcqOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, MTouchOBJInf16, 34, MTouchOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, KeyArrayOBJInf16, 22, KeyArrayOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, ComconfigOBJInf16, 2, ComconfigOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, NoiseOBJInf16, 17, NoiseOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, OneTGOBJInf16, 19, OneTGOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, SelfTestOBJInf16, 14, SelfTestOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, TwoTGOBJInf16, 7, TwoTGOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, CTEOBJInf16, 6, CTEOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, GripSUPOBJInf16, 5, GripSUPOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, PalmSUPOBJInf16, 6, PalmSUPOBJ) < 0)
		return -1;

	if(mxt_write_block(mxt->client, DigitizerOBJInf16, 6, DigitizerOBJ) < 0)
		return -1;

	return 0;
}

/*  boot argument is for determine when to use request_irq or not*/
static int ATMEL_SyncWithThreadToReadMsg(struct mxt_data *mxt, int boot)
{
	int error, count = 0;

	mxt_debug(DEBUG_DETAIL, "ATMEL_SyncWithThreadToReadMsg, boot is %d\n", boot);

	if(boot) {
		mxt_debug(DEBUG_DETAIL, "maXTouch1386: boot request_irq\n");

		if(down_interruptible(&mxt->sema))
			return -1;
		error = request_irq(mxt->irq,
				mxt_irq_handler,
				IRQF_ONESHOT | IRQF_TRIGGER_LOW,
				mxt->client->name,
				mxt);

		if (error < 0) {
			mxt_debug(DEBUG_ERROR, "maXTouch1386: failed to allocate irq %d\n", mxt->irq);
			up(&mxt->sema);
			return -1;
		}
		/* no irq means that touch firmware may be crashed, return fail */
		while(!irq_coming) {
			mdelay(100);
			count++;
			if (count == 20) {
				mxt_debug(DEBUG_ERROR,
						"maXTouch1386: no interrupt, there may be no device or crashed\n");
				up(&mxt->sema);
				return -1;
			}
		}
	} else {
		mxt_debug(DEBUG_DETAIL, "maXTouch1386: enable_irq\n");
		enable_irq(mxt->irq);
	}

	mxt_debug(DEBUG_DETAIL, "PowerOnRead_noaddr before 2nd sema\n");
	if(down_interruptible(&mxt->sema))
		return -1;
	if(i2cfail_real == 1)
		return -1;
	return 0;
}

static int ATMEL_ConfigErrRecovery(struct mxt_data *mxt)
{
	if (ATMEL_SetCTE(mxt) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_SetCTE failed\n");
		return -1;
	}

	if (ATMEL_WriteConfig(mxt) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_WriteConfig failed\n");
		return -1;
	}

	if (ATMEL_Backup(mxt) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_Backup failed\n");
		return -1;
	}

	mdelay(200);

	if (ATMEL_Reset(mxt) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_Reset failed\n");
		return -1;
	}

	mdelay(200);

	mxt->irq_type = ATMEL_ReadResponseMsg;
	if(ATMEL_SyncWithThreadToReadMsg(mxt,0) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_SyncWithThreadToReadMsg failed\n");
		return -1;
	}

	return 0;
}

static int ATMEL_CheckConfig(struct mxt_data *mxt)
{
	if (ATMEL_SendCMDgetCRC(mxt) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_WriteConfig failed\n");
		return -1;
	}

	/* write config first time and check config error path */
	if (ConfigChecksumError) {
		do {
			if (ATMEL_WriteConfig(mxt) < 0) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_WriteConfig failed\n");
				return -1;
			}

			if (ATMEL_Backup(mxt) < 0) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_Backup failed\n");
				return -1;
			}

			mxt->irq_type = ATMEL_ReadResponseMsg;
			if (ATMEL_SyncWithThreadToReadMsg(mxt,0) < 0) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_SyncWithThreadToReadMsg failed\n");
				return -1;
			}

			if(ConfigError) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: ConfigError after Backup\n");
				do {
					if (ATMEL_ConfigErrRecovery(mxt) < 0) {
						mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_ConfigErrRecovery failed\n");
						return -1;
					}

				} while(ConfigError);
			}

			if(ATMEL_SendCMDgetCRC(mxt) < 0) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_WriteConfig failed\n");
				return -1;
			}
		} while(ConfigChecksumError);
	}

	return 0;
}

static int ATMEL_Initial(struct mxt_data *mxt)
{
	u8 sens_buf[8] = {0};
	u8 sens_val[1];
	u16 sens_addr;

	mxt->irq_type = ATMEL_ReadResponseMsg_Noaddress;
	if (ATMEL_SyncWithThreadToReadMsg(mxt,1) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_SyncWithThreadToReadMsg failed\n");
		return -1;
	}

	if (ConfigError) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ConfigError\n");
		if (ATMEL_ReadIDInfo(mxt) < 0) {
			mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_ReadIDInfo failed\n");
			return -1;
		}

		if (ATMEL_CheckOBJTableCRC(mxt) < 0) {
			mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_CheckOBJTableCRC failed\n");
			return -1;
		}
		/* Read the sensitivity value */
		if (mxt_read_block(mxt->client, MTouchOBJInf16, 8, sens_buf) < 0)
			mxt_debug(DEBUG_ERROR, "Read Multiple Touch fail\n");
		sens_val[0] = sens_buf[7];
		mxt_debug(DEBUG_DETAIL, "sensitivity value: 0x%x\n",sens_buf[7]);

		do {
			if (ATMEL_ConfigErrRecovery(mxt) < 0) {
				mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_ConfigErrRecovery failed\n");
				return -1;
			}
		}while(ConfigError);
	} else {
		if (ATMEL_ReadIDInfo(mxt) < 0) {
			mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_ReadIDInfo failed\n");
			return -1;
		}

		if (ATMEL_CheckOBJTableCRC(mxt) < 0) {
			mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_CheckOBJTableCRC failed\n");
			return -1;
		}
		/* Read the sensitivity value */
		if (mxt_read_block(mxt->client, MTouchOBJInf16, 8, sens_buf) < 0)
			mxt_debug(DEBUG_ERROR, "Read Multiple Touch fail\n");
		sens_val[0] = sens_buf[7];
		mxt_debug(DEBUG_DETAIL, "sensitivity value: 0x%x\n",sens_buf[7]);
	}

	/* reserve this for write config to rom in the future */
	if (ATMEL_CheckConfig(mxt) < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_CheckConfig failed\n");
		return -1;
	}

	/* Write the sensitivity value that the same as the AP */
	CalculateAddr16bits(MTouchOBJInf[2], MTouchOBJInf[1], &sens_addr, 7);
	if(mxt_write_block(mxt->client, sens_addr, 1, sens_val) < 0)
		mxt_debug(DEBUG_ERROR, "Write Multiple Touch fail\n");
	if (ATMEL_Backup(mxt) < 0)
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_Backup failed\n");

	mxt->irq_type = ATMEL_HandleTouchMsg;

	enable_irq(mxt->irq);

	return 0;
}
static int ATMEL_Deepsleep(struct mxt_data *mxt)
{
	u8 val[2] = {0, 0};

	mxt_debug(DEBUG_DETAIL, "maXTouch1386: ATMEL_Deepsleep\n");

	if (mxt_write_block(mxt->client, PowerOBJInf16, 2, val) < 0)
		return -1;

	return 0;

}

static int ATMEL_Issleep(struct mxt_data *mxt)
{
	u8 buffer[2] = {0};

	if (mxt_read_block(mxt->client, PowerOBJInf16, 2, buffer) < 0)
		return -1;

	if (buffer[0] != 0 || buffer[1] != 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: touch panel does not sleep\n");
		return 0;
	}

	return 1;

}

static int ATMEL_IsResume(struct mxt_data *mxt)
{
	u8 buffer[2] = {0};

	if (mxt_read_block(mxt->client, PowerOBJInf16, 2, buffer) < 0)
		return -1;

	if (buffer[0] == 0 || buffer[1] == 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: touch panel does not resume, still sleep\n");
		return 0;
	}

	return 1;

}

static int ATMEL_Resume(struct mxt_data *mxt)
{
	u8 val[2] = {0};

	val[0] = PowerOBJ[0];
	val[1] = PowerOBJ[1];

	mxt_debug(DEBUG_DETAIL, "maXTouch1386: ATMEL_Resume\n");

	if (mxt_write_block(mxt->client, PowerOBJInf16, 2, val) < 0)
		return -1;

	return 0;

}

static int mxt_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct input_dev *input;
	int error, i;

	mxt_debug(DEBUG_DETAIL, "maXTouch1386: mxt_probe\n");

	if (client == NULL) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: client == NULL\n");
		return	-EINVAL;
	} else if (client->adapter == NULL) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: client->adapter == NULL\n");
		return	-EINVAL;
	} else if (&client->dev == NULL) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: client->dev == NULL\n");
		return	-EINVAL;
	} else if (&client->adapter->dev == NULL) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: client->adapter->dev == NULL\n");
		return	-EINVAL;
	} else if (id == NULL) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: id == NULL\n");
		return	-EINVAL;
	}

	mxt = kzalloc(sizeof(struct mxt_data), GFP_KERNEL);
	if (mxt == NULL) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: insufficient memory\n");
		error = -ENOMEM;
		goto err_mxt_alloc;
	}

	input = input_allocate_device();
	if (!input) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: error allocating input device\n");
		error = -ENOMEM;
		goto err_input_dev_alloc;
	}

	input->name = "atmel-maxtouch";
	mxt->input  = input;
	mxt->client = client;
	mxt->irq = client->irq;

	INIT_DELAYED_WORK(&mxt->dwork, mxt_worker);

	set_bit(BTN_TOUCH, input->keybit);
	set_bit(EV_ABS, input->evbit);
	set_bit(EV_SYN, input->evbit);
	set_bit(EV_KEY, input->evbit);

	set_bit(ABS_MT_TOUCH_MAJOR, input->keybit);
	set_bit(ABS_MT_POSITION_X, input->keybit);
	set_bit(ABS_MT_POSITION_Y, input->keybit);
	set_bit(ABS_X, input->keybit);
	set_bit(ABS_Y, input->keybit);

	/* single touch */
	input_set_abs_params(input, ABS_X, X_MIN, X_MAX, 0, 0);
	input_set_abs_params(input, ABS_Y, Y_MIN, Y_MAX, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, MXT_MAX_REPORTED_PRESSURE, 0, 0);

	/* multiple touch */
	input_set_abs_params(input, ABS_MT_POSITION_X, X_MIN, X_MAX, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, Y_MIN, Y_MAX, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, MXT_MAX_TOUCH_SIZE, 0, 0);
	input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, NUM_FINGERS_SUPPORTED,0, 0);

	i2c_set_clientdata(client, mxt);

	error = input_register_device(mxt->input);
	if (error < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: Failed to register input device\n");
		goto err_register_device;
	}

	init_MUTEX(&mxt->sema);

	for(i=0;i<NUM_FINGERS_SUPPORTED;i++) {
		mxt->PointBuf[i].X = 0;
		mxt->PointBuf[i].Y = 0;
		mxt->PointBuf[i].Status = -1;
	}

	error = ATMEL_Initial(mxt);
	if (error < 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_Initial failed\n");
		goto err_irq;
	}

	/* SYSFS_START */
	touchdebug_kobj = kobject_create_and_add("Touch", NULL);
	if (touchdebug_kobj == NULL)
		mxt_debug(DEBUG_ERROR, "%s: subsystem_register failed\n", __FUNCTION__);

	error = sysfs_create_group(touchdebug_kobj, &attr_group);
	if(error)
		mxt_debug(DEBUG_ERROR, "%s: sysfs_create_group failed, %d\n", __FUNCTION__, __LINE__);

	/* SYSFS_END */

#ifdef CONFIG_HAS_EARLYSUSPEND
	mxt->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	mxt->early_suspend.suspend = mxt_early_suspend;
	mxt->early_suspend.resume = mxt_late_resume;
	register_early_suspend(&mxt->early_suspend);
#endif
	return 0;

err_irq:
	if (mxt->irq)
	   free_irq(mxt->irq, mxt);
err_register_device:
	input_free_device(input);
err_input_dev_alloc:
	kfree(mxt);
err_mxt_alloc:
	return error;
}

static int mxt_remove(struct i2c_client *client)
{
	struct mxt_data *mxt;

	mxt = i2c_get_clientdata(client);

	if (mxt != NULL) {
		if (mxt->irq)
			free_irq(mxt->irq, mxt);

		cancel_delayed_work_sync(&mxt->dwork);
		input_unregister_device(mxt->input);
	}
	kfree(mxt);
	i2c_set_clientdata(client, NULL);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void mxt_early_suspend(struct early_suspend *h)
{
	struct mxt_data *data = container_of(h, struct mxt_data, early_suspend);
	int ret = 0;
	mxt_suspend = data;
	suspend_resume = 0;
	mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_early_suspend\n");

	cancel_delayed_work_sync(&data->dwork);
	disable_irq(data->irq);
	cancel_delayed_work_sync(&data->dwork);
	/*
	system will still go to suspend if i2c error,
	but it will be blocked if sleep configs are not written to touch successfully
	*/
	if (ATMEL_Deepsleep(data) == 0) {
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_Deepsleep OK!\n");
	} else {
		ret = ATMEL_Issleep(data);
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL SUSPEND Fail: %d\n", ret);
	}
}

void mxt_late_resume(struct early_suspend *h)
{
	struct mxt_data *data = container_of(h, struct mxt_data, early_suspend);
	int i, ret;
	mxt_suspend = data;
	suspend_resume = 1;
	mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_late_resume\n");

	for(i=0;i<NUM_FINGERS_SUPPORTED;i++) {
		data->PointBuf[i].X = 0;
		data->PointBuf[i].Y = 0;
		data->PointBuf[i].Status = -1;
	}
	/*
	system will still resume back if i2c error,
	but it will be blocked if resume configs are not written to touch successfully
	*/
	if (ATMEL_Resume(data) == 0) {
		ret = ATMEL_IsResume(data);
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL_IsResume OK! ret:%d\n", ret);
	} else {
		ret = ATMEL_IsResume(data);
		mxt_debug(DEBUG_ERROR, "maXTouch1386: ATMEL Resume Fail: %d\n", ret);
	}

	if(ATMEL_Calibrate(data) < 0)
		mxt_debug(DEBUG_ERROR, "maXTouch1386: calibration failed\n");

	enable_irq(data->irq);

}
#endif
#ifndef CONFIG_HAS_EARLYSUSPEND
static int mxt_suspend(struct i2c_client *client, pm_message_t mesg)
{
	mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_suspend\n");
	return 0;
}

static int mxt_resume(struct i2c_client *client)
{
	mxt_debug(DEBUG_ERROR, "maXTouch1386: mxt_resume\n");
	return 0;
}
#endif
static const struct i2c_device_id mxt_id[] =
{
	{ "maXTouch", 0 },
	{},
};

static struct i2c_driver mxt_driver =
{
	.probe      = mxt_probe,
	.remove     = mxt_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend    = mxt_suspend,
	.resume     = mxt_resume,
#endif
	.id_table   = mxt_id,
	.driver     = {
		.name   = "maXTouch",
	},
};

static int __init mxt_init(void)
{
	int ret;
	ret = i2c_add_driver(&mxt_driver);

	return ret;
}

static void __exit mxt_exit(void)
{
	i2c_del_driver(&mxt_driver);
}

module_init(mxt_init);
module_exit(mxt_exit);

MODULE_DESCRIPTION("Driver for Atmel mxt1386 Touchscreen Controller");
MODULE_LICENSE("GPL");
