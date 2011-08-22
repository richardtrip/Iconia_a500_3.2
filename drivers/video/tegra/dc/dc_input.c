/*
 * drivers/video/tegra/overlay/dc_input.c
 *
 * Copyright (c) 2010-2011, NVIDIA Corporation.
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
 * 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>

#include "dc_input.h"

struct dc_input {
	struct input_dev *dev;
	char name[40];
	atomic_t listeners;
};

static int dc_input_open(struct input_dev *dev)
{
	struct dc_input *data = input_get_drvdata(dev);
	if (data)
		atomic_inc(&data->listeners);
	return 0;
}

static void dc_input_close(struct input_dev *dev)
{
	struct dc_input *data = input_get_drvdata(dev);
	if (data)
		atomic_dec(&data->listeners);
}

struct dc_input *dc_input_alloc()
{
	return kzalloc(sizeof(struct dc_input), GFP_KERNEL);
}

int dc_input_init(struct miscdevice *par_dev,
		struct dc_input *data)
{
	if (!data)
		return -EINVAL;
	atomic_set(&data->listeners, 0);
	data->dev = input_allocate_device();
	if (!data->dev)
		return -ENOMEM;

	snprintf(data->name, sizeof(data->name),
		"%s.input_tick", par_dev->name);


	data->dev->name = data->name;
	/* TODO need to set bus type or parent?*/

	data->dev->open = dc_input_open;
	data->dev->close = dc_input_close;

	__set_bit(EV_SYN, data->dev->evbit);
	__set_bit(EV_MSC, data->dev->evbit);
	__set_bit(MSC_RAW, data->dev->mscbit);

	input_set_drvdata(data->dev, data);
	if (input_register_device(data->dev))
		goto err_reg_failed;

	return 0;

err_reg_failed:
	input_free_device(data->dev);
	data->dev = NULL;
	return -ENOMEM;
}

void dc_input_destroy(struct dc_input *data)
{
	if (!data || !data->dev)
		return;
	input_unregister_device(data->dev);
	input_free_device(data->dev);
	data->dev = NULL;
}

void dc_input_free(struct dc_input *data)
{
	if (!data)
		return;
	kfree(data);
}

int notify_overlay_flip(struct dc_input *data)
{
	int listeners;
	if (!data)
		return -EINVAL;
	listeners = atomic_read(&data->listeners);
	/* if noone is listening, don't send */
	if (listeners) {
		input_event(data->dev, EV_MSC, MSC_RAW, 1);
		input_sync(data->dev);
	}
	return 0;
}
