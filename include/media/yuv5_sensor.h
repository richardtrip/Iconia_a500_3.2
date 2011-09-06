#ifndef ___YUV5_SENSOR_H__
#define ___YUV5_SENSOR_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

/*-------------------------------------------Important---------------------------------------
 * for changing the SENSOR_NAME, you must need to change the owner of the device. For example
 * Please add /dev/mt9d115 0600 media camera in below file
 * ./device/nvidia/ventana/ueventd.ventana.rc
 * Otherwise, ioctl will get permission deny
 * -------------------------------------------Important--------------------------------------
*/
/* mt9p111 */

#define SENSOR_5M_NAME  "mt9p111"
#define DEV(x)          "/dev/"x
#define SENSOR_PATH     DEV(SENSOR_NAME)
#define LOG_NAME(x)     "ImagerODM-"x
#define LOG_TAG         LOG_NAME(SENSOR_NAME)

#define SENSOR_5M_WAIT_MS       0   /* special number to indicate this is wait time require */
#define SENSOR_5M_TABLE_END     1   /* special number to indicate this is end of table */
#define WRITE_REG_DATA8         2   /* special the data width as one byte */
#define WRITE_REG_DATA16        3   /* special the data width as one byte */
#define POLL_REG_BIT            4   /* poll the bit set */
#define SW_FLASH_CHECK_POINT   20

#define SENSOR_5M_MAX_RETRIES   3      /* max counter for retry I2C access */
#define SENSOR_5M_POLL_RETRIES  20000  /* max poll retry */
#define SENSOR_5M_POLL_WAITMS   1      /* poll wait time */

#define SENSOR_5M_IOCTL_SET_MODE           _IOW('o', 1, struct sensor_5m_mode)
#define SENSOR_5M_IOCTL_GET_STATUS         _IOR('o', 2, __u8)
#define SENSOR_5M_IOCTL_SET_COLOR_EFFECT   _IOW('o', 3, __u8)
#define SENSOR_5M_IOCTL_SET_WHITE_BALANCE  _IOW('o', 4, __u8)
#define SENSOR_5M_IOCTL_SET_SCENE_MODE     _IOW('o', 5, __u8)
#define SENSOR_5M_IOCTL_SET_AF_MODE        _IOW('o', 6, __u8)
#define SENSOR_5M_IOCTL_GET_AF_STATUS      _IOW('o', 7, __u8)
#define SENSOR_5M_IOCTL_SET_EXPOSURE       _IOW('o', 8, __u8)
#define SENSOR_5M_IOCTL_SET_FLASH_MODE     _IOW('o', 9, __u8)
#define SENSOR_5M_IOCTL_GET_CAPTURE_FRAME_RATE _IOR('o',10, __u8)
#define SENSOR_5M_IOCTL_CAPTURE_CMD        _IOW('o', 11, __u8)
#define SENSOR_5M_IOCTL_GET_FLASH_STATUS   _IOW('o', 12, __u8)

enum {
	YUV_5M_ColorEffect = 0,
	YUV_5M_Whitebalance,
	YUV_5M_SceneMode,
	YUV_5M_Exposure,
	YUV_5M_FlashMode
};

enum {
	YUV_5M_ColorEffect_Invalid = 0,
	YUV_5M_ColorEffect_Aqua,
	YUV_5M_ColorEffect_Blackboard,
	YUV_5M_ColorEffect_Mono,
	YUV_5M_ColorEffect_Negative,
	YUV_5M_ColorEffect_None,
	YUV_5M_ColorEffect_Posterize,
	YUV_5M_ColorEffect_Sepia,
	YUV_5M_ColorEffect_Solarize,
	YUV_5M_ColorEffect_Whiteboard
};

enum {
	YUV_5M_Whitebalance_Invalid = 0,
	YUV_5M_Whitebalance_Auto,
	YUV_5M_Whitebalance_Incandescent,
	YUV_5M_Whitebalance_Fluorescent,
	YUV_5M_Whitebalance_WarmFluorescent,
	YUV_5M_Whitebalance_Daylight,
	YUV_5M_Whitebalance_CloudyDaylight,
	YUV_5M_Whitebalance_Shade,
	YUV_5M_Whitebalance_Twilight,
	YUV_5M_Whitebalance_Custom
};

enum {
	YUV_5M_SceneMode_Invalid = 0,
	YUV_5M_SceneMode_Auto,
	YUV_5M_SceneMode_Action,
	YUV_5M_SceneMode_Portrait,
	YUV_5M_SceneMode_Landscape,
	YUV_5M_SceneMode_Beach,
	YUV_5M_SceneMode_Candlelight,
	YUV_5M_SceneMode_Fireworks,
	YUV_5M_SceneMode_Night,
	YUV_5M_SceneMode_NightPortrait,
	YUV_5M_SceneMode_Party,
	YUV_5M_SceneMode_Snow,
	YUV_5M_SceneMode_Sports,
	YUV_5M_SceneMode_SteadyPhoto,
	YUV_5M_SceneMode_Sunset,
	YUV_5M_SceneMode_Theatre,
	YUV_5M_SceneMode_Barcode
};

enum {
	YUV_5M_FlashControlOn = 0,
	YUV_5M_FlashControlOff,
	YUV_5M_FlashControlAuto,
	YUV_5M_FlashControlRedEyeReduction,
	YUV_5M_FlashControlFillin,
	YUV_5M_FlashControlTorch,
};

struct sensor_5m_mode {
	int xres;
	int yres;
};

#ifdef __KERNEL__
struct yuv5_sensor_platform_data {
	int (*power_on)(void);
	int (*power_off)(void);

};
#endif /* __KERNEL__ */

#endif  /* __YUV5_SENSOR_H__ */
