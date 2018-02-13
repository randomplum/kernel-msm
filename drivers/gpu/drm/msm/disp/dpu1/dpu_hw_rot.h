/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DPU_HW_ROT_H
#define _DPU_HW_ROT_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_hw_blk.h"

#define DPU_HW_ROT_NAME_SIZE	80

struct dpu_hw_rot;

/**
 * enum dpu_hw_rot_cmd_type - type of rotator hardware command
 * @DPU_HW_ROT_CMD_VALDIATE: validate rotator command; do not commit
 * @DPU_HW_ROT_CMD_COMMIT: commit/execute rotator command
 * @DPU_HW_ROT_CMD_START: mdp is ready to start
 * @DPU_HW_ROT_CMD_CLEANUP: cleanup rotator command after it is done
 */
enum dpu_hw_rot_cmd_type {
	DPU_HW_ROT_CMD_VALIDATE,
	DPU_HW_ROT_CMD_COMMIT,
	DPU_HW_ROT_CMD_START,
	DPU_HW_ROT_CMD_CLEANUP,
};

/**
 * struct dpu_hw_rot_cmd - definition of hardware rotation command
 * @master: true if client is the master in source split inline rotation
 * @sequence_id: command sequence identifier
 * @fps: frame rate of the stream in frame per second
 * @rot90: true if rotation 90 in counter clockwise is required
 * @hflip: true if horizontal flip is required prior to rotation
 * @vflip: true if vertical flip is required prior to rotation
 * @secure: true if image content is in secure domain
 * @video_mode: true if rotator is feeding into video interface
 * @clkrate : clock rate in Hz
 * @prefill_bw: prefill bandwidth in Bps (video mode only)
 * @src_iova: source i/o virtual address
 * @src_len: source i/o buffer length
 * @src_planes: source plane number
 * @src_format: pointer to source dpu pixel format
 * @src_pixel_format: source pixel format in drm fourcc
 * @src_modifier: source pixel format modifier
 * @src_width: source width in pixel
 * @src_height: source height in pixel
 * @src_rect_x: source rectangle x coordinate
 * @src_rect_y: source rectangle y coordinate
 * @src_rect_w: source rectangle width
 * @src_rect_h: source rectangle height
 * @dst_writeback: true if writeback of rotated output is required
 * @dst_iova: destination i/o virtual address
 * @dst_len: destination i/o buffer length
 * @dst_planes: destination plane number
 * @dst_format: pointer to destination dpu pixel format (input/output)
 * @dst_pixel_format: destination pixel format in drm fourcc (input/output)
 * @dst_modifier: destination pixel format modifier (input/output)
 * @dst_rect_x: destination rectangle x coordinate
 * @dst_rect_y: destination rectangle y coordinate
 * @dst_rect_w: destination rectangle width
 * @dst_rect_h: destination rectangle height
 * @priv_handle: private handle of rotator driver (output)
 */
struct dpu_hw_rot_cmd {
	bool master;
	u32 sequence_id;
	u32 fps;
	bool rot90;
	bool hflip;
	bool vflip;
	bool secure;
	bool video_mode;
	u64 clkrate;
	u64 prefill_bw;
	dma_addr_t src_iova[4];
	u32 src_len[4];
	u32 src_planes;
	const struct dpu_format *src_format;
	u32 src_pixel_format;
	u64 src_modifier;
	u32 src_width;
	u32 src_height;
	u32 src_stride;
	u32 src_rect_x;
	u32 src_rect_y;
	u32 src_rect_w;
	u32 src_rect_h;
	bool dst_writeback;
	dma_addr_t dst_iova[4];
	u32 dst_len[4];
	u32 dst_planes;
	const struct dpu_format *dst_format;
	u32 dst_pixel_format;
	u64 dst_modifier;
	u32 dst_rect_x;
	u32 dst_rect_y;
	u32 dst_rect_w;
	u32 dst_rect_h;
	void *priv_handle;
};

/**
 * struct dpu_hw_rot_ops - interface to the rotator hw driver functions
 * Assumption is these functions will be called after clocks are enabled
 */
struct dpu_hw_rot_ops {
	int (*commit)(struct dpu_hw_rot *hw, struct dpu_hw_rot_cmd *data,
			enum dpu_hw_rot_cmd_type cmd);
	const struct dpu_format_extended *(*get_format_caps)(
			struct dpu_hw_rot *hw);
	const char *(*get_downscale_caps)(struct dpu_hw_rot *hw);
	size_t (*get_cache_size)(struct dpu_hw_rot *hw);
	int (*get_maxlinewidth)(struct dpu_hw_rot *hw);
};

/**
 * struct dpu_hw_rot : ROT driver object
 * @base: hw block base object
 * @hw: hardware address map
 * @idx: instance index
 * @caps: capabilities bitmask
 * @ops: operation table
 * @rot_ctx: pointer to private rotator context
 * @format_caps: pointer to pixel format capability  array
 * @downscale_caps: pointer to scaling capability string
 */
struct dpu_hw_rot {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;
	char name[DPU_HW_ROT_NAME_SIZE];
	int idx;
	const struct dpu_rot_cfg *caps;
	struct dpu_hw_rot_ops ops;
	void *rot_ctx;
	struct dpu_format_extended *format_caps;
	char *downscale_caps;
};

/**
 * dpu_hw_rot_init - initialize and return rotator hw driver object.
 * @idx:  wb_path index for which driver object is required
 * @addr: mapped register io address of MDP
 * @m :   pointer to mdss catalog data
 */
struct dpu_hw_rot *dpu_hw_rot_init(enum dpu_rot idx,
		void __iomem *addr,
		struct dpu_mdss_cfg *m);

/**
 * dpu_hw_rot_destroy - destroy rotator hw driver object.
 * @hw_rot:  Pointer to rotator hw driver object
 */
void dpu_hw_rot_destroy(struct dpu_hw_rot *hw_rot);

/**
 * to_dpu_hw_rot - convert base object dpu_hw_base to rotator object
 * @hw: Pointer to base hardware block
 * return: Pointer to rotator hardware block
 */
static inline struct dpu_hw_rot *to_dpu_hw_rot(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_rot, base);
}

/**
 * dpu_hw_rot_get - get next available hardware rotator, or increment reference
 *	count if hardware rotator provided
 * @hw_rot: Pointer to hardware rotator
 * return: Pointer to rotator hardware block if success; NULL otherwise
 */
struct dpu_hw_rot *dpu_hw_rot_get(struct dpu_hw_rot *hw_rot);

/**
 * dpu_hw_rot_put - put the given hardware rotator
 * @hw_rot: Pointer to hardware rotator
 * return: none
 */
void dpu_hw_rot_put(struct dpu_hw_rot *hw_rot);

#endif /*_DPU_HW_ROT_H */
