/**
 * Copyright (c) 2008 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef __LTC3216_H__
#define __LTC3216_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

#define LTC3216_IOCTL_FLASH_ON   _IOW('o', 1, __u16)
#define LTC3216_IOCTL_FLASH_OFF  _IOW('o', 2, __u16)
#define LTC3216_IOCTL_TORCH_ON   _IOW('o', 3, __u16)
#define LTC3216_IOCTL_TORCH_OFF  _IOW('o', 4, __u16)

#endif  /* __OV5650_H__ */
