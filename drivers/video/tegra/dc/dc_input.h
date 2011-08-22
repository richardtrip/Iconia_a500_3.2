/*
 * drivers/video/tegra/overlay/dc_input.h
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

#ifndef DC_INPUT_H
#define DC_INPUT_H

struct dc_input;
struct miscdevice;

struct dc_input *dc_input_alloc();
int dc_input_init(struct miscdevice *par_dev, struct dc_input *data);

int notify_overlay_flip(struct dc_input *data);

void dc_input_destroy(struct dc_input *data);
void dc_input_free(struct dc_input *data);

#endif /*DC_INPUT_H*/
