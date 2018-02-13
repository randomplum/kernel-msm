/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#if IS_ENABLED(CONFIG_DRM_MSM_ROTATOR)
#include <linux/soc/qcom/llcc-qcom.h>
#endif
#include "dpu_hw_mdss.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_catalog_format.h"
#include "dpu_kms.h"

/*************************************************************
 * MACRO DEFINITION
 *************************************************************/

/**
 * Max hardware block in certain hardware. For ex: sspp pipes
 * can have QSEED, pcc, igc, pa, csc, qos entries, etc. This count is
 * 64 based on software design. It should be increased if any of the
 * hardware block has more subblocks.
 */
#define MAX_DPU_HW_BLK  64

/* each entry will have register address and bit offset in that register */
#define MAX_BIT_OFFSET 2

/* default line width for sspp, mixer, ds (input), wb */
#define DEFAULT_DPU_LINE_WIDTH 2048

/* default output line width for ds */
#define DEFAULT_DPU_OUTPUT_LINE_WIDTH 2560

/* max mixer blend stages */
#define DEFAULT_DPU_MIXER_BLENDSTAGES 7

/* max bank bit for macro tile and ubwc format */
#define DEFAULT_DPU_HIGHEST_BANK_BIT 15

/* default ubwc version */
#define DEFAULT_DPU_UBWC_VERSION DPU_HW_UBWC_VER_10

/* default ubwc static config register value */
#define DEFAULT_DPU_UBWC_STATIC 0x0

/* default ubwc swizzle register value */
#define DEFAULT_DPU_UBWC_SWIZZLE 0x0

/* default hardware block size if dtsi entry is not present */
#define DEFAULT_DPU_HW_BLOCK_LEN 0x100

/* total number of intf - dp, dsi, hdmi */
#define INTF_COUNT			3

#define MAX_UPSCALE_RATIO		20
#define MAX_DOWNSCALE_RATIO		4
#define SSPP_UNITY_SCALE		1

#define MAX_HORZ_DECIMATION		4
#define MAX_VERT_DECIMATION		4

#define MAX_SPLIT_DISPLAY_CTL		2
#define MAX_PP_SPLIT_DISPLAY_CTL	1

#define MDSS_BASE_OFFSET		0x0

#define ROT_LM_OFFSET			3
#define LINE_LM_OFFSET			5
#define LINE_MODE_WB_OFFSET		2

/* maximum XIN halt timeout in usec */
#define VBIF_XIN_HALT_TIMEOUT		0x4000

#define DEFAULT_PIXEL_RAM_SIZE		(50 * 1024)

/* access property value based on prop_type and hardware index */
#define PROP_VALUE_ACCESS(p, i, j)		((p + i)->value[j])

/*
 * access element within PROP_TYPE_BIT_OFFSET_ARRAYs based on prop_type,
 * hardware index and offset array index
 */
#define PROP_BITVALUE_ACCESS(p, i, j, k)	((p + i)->bit_value[j][k])

#define DEFAULT_SBUF_HEADROOM		(20)

/*
 * Default parameter values
 */
#define DEFAULT_MAX_BW_HIGH			7000000
#define DEFAULT_MAX_BW_LOW			7000000
#define DEFAULT_UNDERSIZED_PREFILL_LINES	2
#define DEFAULT_XTRA_PREFILL_LINES		2
#define DEFAULT_DEST_SCALE_PREFILL_LINES	3
#define DEFAULT_MACROTILE_PREFILL_LINES		4
#define DEFAULT_YUV_NV12_PREFILL_LINES		8
#define DEFAULT_LINEAR_PREFILL_LINES		1
#define DEFAULT_DOWNSCALING_PREFILL_LINES	1
#define DEFAULT_CORE_IB_FF			"6.0"
#define DEFAULT_CORE_CLK_FF			"1.0"
#define DEFAULT_COMP_RATIO_RT \
		"NV12/5/1/1.23 AB24/5/1/1.23 XB24/5/1/1.23"
#define DEFAULT_COMP_RATIO_NRT \
		"NV12/5/1/1.25 AB24/5/1/1.25 XB24/5/1/1.25"
#define DEFAULT_MAX_PER_PIPE_BW			2400000
#define DEFAULT_AMORTIZABLE_THRESHOLD		25

/*************************************************************
 *  DTSI PROPERTY INDEX
 *************************************************************/
enum {
	HW_OFF,
	HW_LEN,
	HW_PROP_MAX,
};

enum dpu_prop {
	DPU_OFF,
	DPU_LEN,
	SSPP_LINEWIDTH,
	MIXER_LINEWIDTH,
	MIXER_BLEND,
	WB_LINEWIDTH,
	BANK_BIT,
	UBWC_VERSION,
	UBWC_STATIC,
	UBWC_SWIZZLE,
	QSEED_TYPE,
	CSC_TYPE,
	PANIC_PER_PIPE,
	SRC_SPLIT,
	DIM_LAYER,
	SMART_DMA_REV,
	IDLE_PC,
	DEST_SCALER,
	DPU_PROP_MAX,
};

enum {
	PERF_MAX_BW_LOW,
	PERF_MAX_BW_HIGH,
	PERF_MIN_CORE_IB,
	PERF_MIN_LLCC_IB,
	PERF_MIN_DRAM_IB,
	PERF_CORE_IB_FF,
	PERF_CORE_CLK_FF,
	PERF_COMP_RATIO_RT,
	PERF_COMP_RATIO_NRT,
	PERF_UNDERSIZED_PREFILL_LINES,
	PERF_DEST_SCALE_PREFILL_LINES,
	PERF_MACROTILE_PREFILL_LINES,
	PERF_YUV_NV12_PREFILL_LINES,
	PERF_LINEAR_PREFILL_LINES,
	PERF_DOWNSCALING_PREFILL_LINES,
	PERF_XTRA_PREFILL_LINES,
	PERF_AMORTIZABLE_THRESHOLD,
	PERF_DANGER_LUT,
	PERF_SAFE_LUT,
	PERF_QOS_LUT_LINEAR,
	PERF_QOS_LUT_MACROTILE,
	PERF_QOS_LUT_NRT,
	PERF_QOS_LUT_CWB,
	PERF_CDP_SETTING,
	PERF_PROP_MAX,
};

enum {
	SSPP_OFF,
	SSPP_SIZE,
	SSPP_TYPE,
	SSPP_XIN,
	SSPP_CLK_CTRL,
	SSPP_CLK_STATUS,
	SSPP_SCALE_SIZE,
	SSPP_VIG_BLOCKS,
	SSPP_RGB_BLOCKS,
	SSPP_EXCL_RECT,
	SSPP_SMART_DMA,
	SSPP_MAX_PER_PIPE_BW,
	SSPP_PROP_MAX,
};

enum {
	VIG_QSEED_OFF,
	VIG_QSEED_LEN,
	VIG_CSC_OFF,
	VIG_HSIC_PROP,
	VIG_MEMCOLOR_PROP,
	VIG_PCC_PROP,
	VIG_PROP_MAX,
};

enum {
	RGB_SCALER_OFF,
	RGB_SCALER_LEN,
	RGB_PCC_PROP,
	RGB_PROP_MAX,
};

enum {
	INTF_OFF,
	INTF_LEN,
	INTF_PREFETCH,
	INTF_TYPE,
	INTF_PROP_MAX,
};

enum {
	PP_OFF,
	PP_LEN,
	TE_OFF,
	TE_LEN,
	TE2_OFF,
	TE2_LEN,
	PP_SLAVE,
	DITHER_OFF,
	DITHER_LEN,
	DITHER_VER,
	PP_PROP_MAX,
};

enum {
	DSC_OFF,
	DSC_LEN,
	DSC_PROP_MAX,
};

enum {
	DS_TOP_OFF,
	DS_TOP_LEN,
	DS_TOP_INPUT_LINEWIDTH,
	DS_TOP_OUTPUT_LINEWIDTH,
	DS_TOP_PROP_MAX,
};

enum {
	DS_OFF,
	DS_LEN,
	DS_PROP_MAX,
};

enum {
	DSPP_TOP_OFF,
	DSPP_TOP_SIZE,
	DSPP_TOP_PROP_MAX,
};

enum {
	DSPP_OFF,
	DSPP_SIZE,
	DSPP_BLOCKS,
	DSPP_PROP_MAX,
};

enum {
	DSPP_IGC_PROP,
	DSPP_PCC_PROP,
	DSPP_GC_PROP,
	DSPP_HSIC_PROP,
	DSPP_MEMCOLOR_PROP,
	DSPP_SIXZONE_PROP,
	DSPP_GAMUT_PROP,
	DSPP_DITHER_PROP,
	DSPP_HIST_PROP,
	DSPP_VLUT_PROP,
	DSPP_BLOCKS_PROP_MAX,
};

enum {
	AD_OFF,
	AD_VERSION,
	AD_PROP_MAX,
};

enum {
	MIXER_OFF,
	MIXER_LEN,
	MIXER_PAIR_MASK,
	MIXER_BLOCKS,
	MIXER_PROP_MAX,
};

enum {
	MIXER_GC_PROP,
	MIXER_BLOCKS_PROP_MAX,
};

enum {
	MIXER_BLEND_OP_OFF,
	MIXER_BLEND_PROP_MAX,
};

enum {
	WB_OFF,
	WB_LEN,
	WB_ID,
	WB_XIN_ID,
	WB_CLK_CTRL,
	WB_PROP_MAX,
};

enum {
	VBIF_OFF,
	VBIF_LEN,
	VBIF_ID,
	VBIF_DEFAULT_OT_RD_LIMIT,
	VBIF_DEFAULT_OT_WR_LIMIT,
	VBIF_DYNAMIC_OT_RD_LIMIT,
	VBIF_DYNAMIC_OT_WR_LIMIT,
	VBIF_QOS_RT_REMAP,
	VBIF_QOS_NRT_REMAP,
	VBIF_MEMTYPE_0,
	VBIF_MEMTYPE_1,
	VBIF_PROP_MAX,
};

enum {
	REG_DMA_OFF,
	REG_DMA_VERSION,
	REG_DMA_TRIGGER_OFF,
	REG_DMA_PROP_MAX
};

enum {
	INLINE_ROT_XIN,
	INLINE_ROT_XIN_TYPE,
	INLINE_ROT_CLK_CTRL,
	INLINE_ROT_PROP_MAX
};

/*************************************************************
 * dts property definition
 *************************************************************/
enum prop_type {
	PROP_TYPE_BOOL,
	PROP_TYPE_U32,
	PROP_TYPE_U32_ARRAY,
	PROP_TYPE_STRING,
	PROP_TYPE_STRING_ARRAY,
	PROP_TYPE_BIT_OFFSET_ARRAY,
	PROP_TYPE_NODE,
};

struct dpu_prop_type {
	/* use property index from enum property for readability purpose */
	u8 id;
	/* it should be property name based on dtsi documentation */
	char *prop_name;
	/**
	 * if property is marked mandatory then it will fail parsing
	 * when property is not present
	 */
	u32  is_mandatory;
	/* property type based on "enum prop_type"  */
	enum prop_type type;
};

struct dpu_prop_value {
	u32 value[MAX_DPU_HW_BLK];
	u32 bit_value[MAX_DPU_HW_BLK][MAX_BIT_OFFSET];
};

/*************************************************************
 * dts property list
 *************************************************************/
static struct dpu_prop_type dpu_prop[] = {
	{DPU_OFF, "qcom,dpu-off", true, PROP_TYPE_U32},
	{DPU_LEN, "qcom,dpu-len", false, PROP_TYPE_U32},
	{SSPP_LINEWIDTH, "qcom,dpu-sspp-linewidth", false, PROP_TYPE_U32},
	{MIXER_LINEWIDTH, "qcom,dpu-mixer-linewidth", false, PROP_TYPE_U32},
	{MIXER_BLEND, "qcom,dpu-mixer-blendstages", false, PROP_TYPE_U32},
	{WB_LINEWIDTH, "qcom,dpu-wb-linewidth", false, PROP_TYPE_U32},
	{BANK_BIT, "qcom,dpu-highest-bank-bit", false, PROP_TYPE_U32},
	{UBWC_VERSION, "qcom,dpu-ubwc-version", false, PROP_TYPE_U32},
	{UBWC_STATIC, "qcom,dpu-ubwc-static", false, PROP_TYPE_U32},
	{UBWC_SWIZZLE, "qcom,dpu-ubwc-swizzle", false, PROP_TYPE_U32},
	{QSEED_TYPE, "qcom,dpu-qseed-type", false, PROP_TYPE_STRING},
	{CSC_TYPE, "qcom,dpu-csc-type", false, PROP_TYPE_STRING},
	{PANIC_PER_PIPE, "qcom,dpu-panic-per-pipe", false, PROP_TYPE_BOOL},
	{SRC_SPLIT, "qcom,dpu-has-src-split", false, PROP_TYPE_BOOL},
	{DIM_LAYER, "qcom,dpu-has-dim-layer", false, PROP_TYPE_BOOL},
	{SMART_DMA_REV, "qcom,dpu-smart-dma-rev", false, PROP_TYPE_STRING},
	{IDLE_PC, "qcom,dpu-has-idle-pc", false, PROP_TYPE_BOOL},
	{DEST_SCALER, "qcom,dpu-has-dest-scaler", false, PROP_TYPE_BOOL},
};

static struct dpu_prop_type dpu_perf_prop[] = {
	{PERF_MAX_BW_LOW, "qcom,dpu-max-bw-low-kbps", false, PROP_TYPE_U32},
	{PERF_MAX_BW_HIGH, "qcom,dpu-max-bw-high-kbps", false, PROP_TYPE_U32},
	{PERF_MIN_CORE_IB, "qcom,dpu-min-core-ib-kbps", false, PROP_TYPE_U32},
	{PERF_MIN_LLCC_IB, "qcom,dpu-min-llcc-ib-kbps", false, PROP_TYPE_U32},
	{PERF_MIN_DRAM_IB, "qcom,dpu-min-dram-ib-kbps", false, PROP_TYPE_U32},
	{PERF_CORE_IB_FF, "qcom,dpu-core-ib-ff", false, PROP_TYPE_STRING},
	{PERF_CORE_CLK_FF, "qcom,dpu-core-clk-ff", false, PROP_TYPE_STRING},
	{PERF_COMP_RATIO_RT, "qcom,dpu-comp-ratio-rt", false,
			PROP_TYPE_STRING},
	{PERF_COMP_RATIO_NRT, "qcom,dpu-comp-ratio-nrt", false,
			PROP_TYPE_STRING},
	{PERF_UNDERSIZED_PREFILL_LINES, "qcom,dpu-undersizedprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_DEST_SCALE_PREFILL_LINES, "qcom,dpu-dest-scaleprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_MACROTILE_PREFILL_LINES, "qcom,dpu-macrotileprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_YUV_NV12_PREFILL_LINES, "qcom,dpu-yuv-nv12prefill-lines",
			false, PROP_TYPE_U32},
	{PERF_LINEAR_PREFILL_LINES, "qcom,dpu-linearprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_DOWNSCALING_PREFILL_LINES, "qcom,dpu-downscalingprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_XTRA_PREFILL_LINES, "qcom,dpu-xtra-prefill-lines",
			false, PROP_TYPE_U32},
	{PERF_AMORTIZABLE_THRESHOLD, "qcom,dpu-amortizable-threshold",
			false, PROP_TYPE_U32},
	{PERF_DANGER_LUT, "qcom,dpu-danger-lut", false, PROP_TYPE_U32_ARRAY},
	{PERF_SAFE_LUT, "qcom,dpu-safe-lut", false, PROP_TYPE_U32_ARRAY},
	{PERF_QOS_LUT_LINEAR, "qcom,dpu-qos-lut-linear", false,
			PROP_TYPE_U32_ARRAY},
	{PERF_QOS_LUT_MACROTILE, "qcom,dpu-qos-lut-macrotile", false,
			PROP_TYPE_U32_ARRAY},
	{PERF_QOS_LUT_NRT, "qcom,dpu-qos-lut-nrt", false,
			PROP_TYPE_U32_ARRAY},
	{PERF_QOS_LUT_CWB, "qcom,dpu-qos-lut-cwb", false,
			PROP_TYPE_U32_ARRAY},
	{PERF_CDP_SETTING, "qcom,dpu-cdp-setting", false,
			PROP_TYPE_U32_ARRAY},
};

static struct dpu_prop_type sspp_prop[] = {
	{SSPP_OFF, "qcom,dpu-sspp-off", true, PROP_TYPE_U32_ARRAY},
	{SSPP_SIZE, "qcom,dpu-sspp-src-size", false, PROP_TYPE_U32},
	{SSPP_TYPE, "qcom,dpu-sspp-type", true, PROP_TYPE_STRING_ARRAY},
	{SSPP_XIN, "qcom,dpu-sspp-xin-id", true, PROP_TYPE_U32_ARRAY},
	{SSPP_CLK_CTRL, "qcom,dpu-sspp-clk-ctrl", false,
		PROP_TYPE_BIT_OFFSET_ARRAY},
	{SSPP_CLK_STATUS, "qcom,dpu-sspp-clk-status", false,
		PROP_TYPE_BIT_OFFSET_ARRAY},
	{SSPP_SCALE_SIZE, "qcom,dpu-sspp-scale-size", false, PROP_TYPE_U32},
	{SSPP_VIG_BLOCKS, "qcom,dpu-sspp-vig-blocks", false, PROP_TYPE_NODE},
	{SSPP_RGB_BLOCKS, "qcom,dpu-sspp-rgb-blocks", false, PROP_TYPE_NODE},
	{SSPP_EXCL_RECT, "qcom,dpu-sspp-excl-rect", false, PROP_TYPE_U32_ARRAY},
	{SSPP_SMART_DMA, "qcom,dpu-sspp-smart-dma-priority", false,
		PROP_TYPE_U32_ARRAY},
	{SSPP_MAX_PER_PIPE_BW, "qcom,dpu-max-per-pipe-bw-kbps", false,
		PROP_TYPE_U32_ARRAY},
};

static struct dpu_prop_type vig_prop[] = {
	{VIG_QSEED_OFF, "qcom,dpu-vig-qseed-off", false, PROP_TYPE_U32},
	{VIG_QSEED_LEN, "qcom,dpu-vig-qseed-size", false, PROP_TYPE_U32},
	{VIG_CSC_OFF, "qcom,dpu-vig-csc-off", false, PROP_TYPE_U32},
	{VIG_HSIC_PROP, "qcom,dpu-vig-hsic", false, PROP_TYPE_U32_ARRAY},
	{VIG_MEMCOLOR_PROP, "qcom,dpu-vig-memcolor", false,
		PROP_TYPE_U32_ARRAY},
	{VIG_PCC_PROP, "qcom,dpu-vig-pcc", false, PROP_TYPE_U32_ARRAY},
};

static struct dpu_prop_type rgb_prop[] = {
	{RGB_SCALER_OFF, "qcom,dpu-rgb-scaler-off", false, PROP_TYPE_U32},
	{RGB_SCALER_LEN, "qcom,dpu-rgb-scaler-size", false, PROP_TYPE_U32},
	{RGB_PCC_PROP, "qcom,dpu-rgb-pcc", false, PROP_TYPE_U32_ARRAY},
};

static struct dpu_prop_type ctl_prop[] = {
	{HW_OFF, "qcom,dpu-ctl-off", true, PROP_TYPE_U32_ARRAY},
	{HW_LEN, "qcom,dpu-ctl-size", false, PROP_TYPE_U32},
};

struct dpu_prop_type mixer_blend_prop[] = {
	{MIXER_BLEND_OP_OFF, "qcom,dpu-mixer-blend-op-off", true,
		PROP_TYPE_U32_ARRAY},
};

static struct dpu_prop_type mixer_prop[] = {
	{MIXER_OFF, "qcom,dpu-mixer-off", true, PROP_TYPE_U32_ARRAY},
	{MIXER_LEN, "qcom,dpu-mixer-size", false, PROP_TYPE_U32},
	{MIXER_PAIR_MASK, "qcom,dpu-mixer-pair-mask", true,
		PROP_TYPE_U32_ARRAY},
	{MIXER_BLOCKS, "qcom,dpu-mixer-blocks", false, PROP_TYPE_NODE},
};

static struct dpu_prop_type mixer_blocks_prop[] = {
	{MIXER_GC_PROP, "qcom,dpu-mixer-gc", false, PROP_TYPE_U32_ARRAY},
};

static struct dpu_prop_type dspp_top_prop[] = {
	{DSPP_TOP_OFF, "qcom,dpu-dspp-top-off", true, PROP_TYPE_U32},
	{DSPP_TOP_SIZE, "qcom,dpu-dspp-top-size", false, PROP_TYPE_U32},
};

static struct dpu_prop_type dspp_prop[] = {
	{DSPP_OFF, "qcom,dpu-dspp-off", true, PROP_TYPE_U32_ARRAY},
	{DSPP_SIZE, "qcom,dpu-dspp-size", false, PROP_TYPE_U32},
	{DSPP_BLOCKS, "qcom,dpu-dspp-blocks", false, PROP_TYPE_NODE},
};

static struct dpu_prop_type dspp_blocks_prop[] = {
	{DSPP_IGC_PROP, "qcom,dpu-dspp-igc", false, PROP_TYPE_U32_ARRAY},
	{DSPP_PCC_PROP, "qcom,dpu-dspp-pcc", false, PROP_TYPE_U32_ARRAY},
	{DSPP_GC_PROP, "qcom,dpu-dspp-gc", false, PROP_TYPE_U32_ARRAY},
	{DSPP_HSIC_PROP, "qcom,dpu-dspp-hsic", false, PROP_TYPE_U32_ARRAY},
	{DSPP_MEMCOLOR_PROP, "qcom,dpu-dspp-memcolor", false,
		PROP_TYPE_U32_ARRAY},
	{DSPP_SIXZONE_PROP, "qcom,dpu-dspp-sixzone", false,
		PROP_TYPE_U32_ARRAY},
	{DSPP_GAMUT_PROP, "qcom,dpu-dspp-gamut", false, PROP_TYPE_U32_ARRAY},
	{DSPP_DITHER_PROP, "qcom,dpu-dspp-dither", false, PROP_TYPE_U32_ARRAY},
	{DSPP_HIST_PROP, "qcom,dpu-dspp-hist", false, PROP_TYPE_U32_ARRAY},
	{DSPP_VLUT_PROP, "qcom,dpu-dspp-vlut", false, PROP_TYPE_U32_ARRAY},
};

static struct dpu_prop_type ad_prop[] = {
	{AD_OFF, "qcom,dpu-dspp-ad-off", false, PROP_TYPE_U32_ARRAY},
	{AD_VERSION, "qcom,dpu-dspp-ad-version", false, PROP_TYPE_U32},
};

static struct dpu_prop_type ds_top_prop[] = {
	{DS_TOP_OFF, "qcom,dpu-dest-scaler-top-off", false, PROP_TYPE_U32},
	{DS_TOP_LEN, "qcom,dpu-dest-scaler-top-size", false, PROP_TYPE_U32},
	{DS_TOP_INPUT_LINEWIDTH, "qcom,dpu-max-dest-scaler-input-linewidth",
		false, PROP_TYPE_U32},
	{DS_TOP_OUTPUT_LINEWIDTH, "qcom,dpu-max-dest-scaler-output-linewidth",
		false, PROP_TYPE_U32},
};

static struct dpu_prop_type ds_prop[] = {
	{DS_OFF, "qcom,dpu-dest-scaler-off", false, PROP_TYPE_U32_ARRAY},
	{DS_LEN, "qcom,dpu-dest-scaler-size", false, PROP_TYPE_U32},
};

static struct dpu_prop_type pp_prop[] = {
	{PP_OFF, "qcom,dpu-pp-off", true, PROP_TYPE_U32_ARRAY},
	{PP_LEN, "qcom,dpu-pp-size", false, PROP_TYPE_U32},
	{TE_OFF, "qcom,dpu-te-off", false, PROP_TYPE_U32_ARRAY},
	{TE_LEN, "qcom,dpu-te-size", false, PROP_TYPE_U32},
	{TE2_OFF, "qcom,dpu-te2-off", false, PROP_TYPE_U32_ARRAY},
	{TE2_LEN, "qcom,dpu-te2-size", false, PROP_TYPE_U32},
	{PP_SLAVE, "qcom,dpu-pp-slave", false, PROP_TYPE_U32_ARRAY},
	{DITHER_OFF, "qcom,dpu-dither-off", false, PROP_TYPE_U32_ARRAY},
	{DITHER_LEN, "qcom,dpu-dither-size", false, PROP_TYPE_U32},
	{DITHER_VER, "qcom,dpu-dither-version", false, PROP_TYPE_U32},
};

static struct dpu_prop_type dsc_prop[] = {
	{DSC_OFF, "qcom,dpu-dsc-off", false, PROP_TYPE_U32_ARRAY},
	{DSC_LEN, "qcom,dpu-dsc-size", false, PROP_TYPE_U32},
};

static struct dpu_prop_type cdm_prop[] = {
	{HW_OFF, "qcom,dpu-cdm-off", false, PROP_TYPE_U32_ARRAY},
	{HW_LEN, "qcom,dpu-cdm-size", false, PROP_TYPE_U32},
};

static struct dpu_prop_type intf_prop[] = {
	{INTF_OFF, "qcom,dpu-intf-off", true, PROP_TYPE_U32_ARRAY},
	{INTF_LEN, "qcom,dpu-intf-size", false, PROP_TYPE_U32},
	{INTF_PREFETCH, "qcom,dpu-intf-max-prefetch-lines", false,
						PROP_TYPE_U32_ARRAY},
	{INTF_TYPE, "qcom,dpu-intf-type", false, PROP_TYPE_STRING_ARRAY},
};

static struct dpu_prop_type wb_prop[] = {
	{WB_OFF, "qcom,dpu-wb-off", true, PROP_TYPE_U32_ARRAY},
	{WB_LEN, "qcom,dpu-wb-size", false, PROP_TYPE_U32},
	{WB_ID, "qcom,dpu-wb-id", true, PROP_TYPE_U32_ARRAY},
	{WB_XIN_ID, "qcom,dpu-wb-xin-id", false, PROP_TYPE_U32_ARRAY},
	{WB_CLK_CTRL, "qcom,dpu-wb-clk-ctrl", false,
		PROP_TYPE_BIT_OFFSET_ARRAY},
};

static struct dpu_prop_type vbif_prop[] = {
	{VBIF_OFF, "qcom,dpu-vbif-off", true, PROP_TYPE_U32_ARRAY},
	{VBIF_LEN, "qcom,dpu-vbif-size", false, PROP_TYPE_U32},
	{VBIF_ID, "qcom,dpu-vbif-id", false, PROP_TYPE_U32_ARRAY},
	{VBIF_DEFAULT_OT_RD_LIMIT, "qcom,dpu-vbif-default-ot-rd-limit", false,
		PROP_TYPE_U32},
	{VBIF_DEFAULT_OT_WR_LIMIT, "qcom,dpu-vbif-default-ot-wr-limit", false,
		PROP_TYPE_U32},
	{VBIF_DYNAMIC_OT_RD_LIMIT, "qcom,dpu-vbif-dynamic-ot-rd-limit", false,
		PROP_TYPE_U32_ARRAY},
	{VBIF_DYNAMIC_OT_WR_LIMIT, "qcom,dpu-vbif-dynamic-ot-wr-limit", false,
		PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_RT_REMAP, "qcom,dpu-vbif-qos-rt-remap", false,
		PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_NRT_REMAP, "qcom,dpu-vbif-qos-nrt-remap", false,
		PROP_TYPE_U32_ARRAY},
	{VBIF_MEMTYPE_0, "qcom,dpu-vbif-memtype-0", false, PROP_TYPE_U32_ARRAY},
	{VBIF_MEMTYPE_1, "qcom,dpu-vbif-memtype-1", false, PROP_TYPE_U32_ARRAY},
};

static struct dpu_prop_type reg_dma_prop[REG_DMA_PROP_MAX] = {
	[REG_DMA_OFF] =  {REG_DMA_OFF, "qcom,dpu-reg-dma-off", false,
		PROP_TYPE_U32},
	[REG_DMA_VERSION] = {REG_DMA_VERSION, "qcom,dpu-reg-dma-version",
		false, PROP_TYPE_U32},
	[REG_DMA_TRIGGER_OFF] = {REG_DMA_TRIGGER_OFF,
		"qcom,dpu-reg-dma-trigger-off", false,
		PROP_TYPE_U32},
};

/*************************************************************
 * static API list
 *************************************************************/

static int _parse_dt_u32_handler(struct device_node *np,
	char *prop_name, u32 *offsets, int len, bool mandatory)
{
	int rc = -EINVAL;

	if (len > MAX_DPU_HW_BLK) {
		DPU_ERROR(
			"prop: %s tries out of bound access for u32 array read len: %d\n",
				prop_name, len);
		return -E2BIG;
	}

	rc = of_property_read_u32_array(np, prop_name, offsets, len);
	if (rc && mandatory)
		DPU_ERROR("mandatory prop: %s u32 array read len:%d\n",
				prop_name, len);
	else if (rc)
		DPU_DEBUG("optional prop: %s u32 array read len:%d\n",
				prop_name, len);

	return rc;
}

static int _parse_dt_bit_offset(struct device_node *np,
	char *prop_name, struct dpu_prop_value *prop_value, u32 prop_index,
	u32 count, bool mandatory)
{
	int rc = 0, len, i, j;
	const u32 *arr;

	arr = of_get_property(np, prop_name, &len);
	if (arr) {
		len /= sizeof(u32);
		len &= ~0x1;

		if (len > (MAX_DPU_HW_BLK * MAX_BIT_OFFSET)) {
			DPU_ERROR(
				"prop: %s len: %d will lead to out of bound access\n",
				prop_name, len / MAX_BIT_OFFSET);
			return -E2BIG;
		}

		for (i = 0, j = 0; i < len; j++) {
			PROP_BITVALUE_ACCESS(prop_value, prop_index, j, 0) =
				be32_to_cpu(arr[i]);
			i++;
			PROP_BITVALUE_ACCESS(prop_value, prop_index, j, 1) =
				be32_to_cpu(arr[i]);
			i++;
		}
	} else {
		if (mandatory) {
			DPU_ERROR("error mandatory property '%s' not found\n",
				prop_name);
			rc = -EINVAL;
		} else {
			DPU_DEBUG("error optional property '%s' not found\n",
				prop_name);
		}
	}

	return rc;
}

static int _validate_dt_entry(struct device_node *np,
	struct dpu_prop_type *dpu_prop, u32 prop_size, int *prop_count,
	int *off_count)
{
	int rc = 0, i, val;
	struct device_node *snp = NULL;

	if (off_count) {
		*off_count = of_property_count_u32_elems(np,
				dpu_prop[0].prop_name);
		if ((*off_count > MAX_BLOCKS) || (*off_count < 0)) {
			if (dpu_prop[0].is_mandatory) {
				DPU_ERROR(
					"invalid hw offset prop name:%s count: %d\n",
					dpu_prop[0].prop_name, *off_count);
				rc = -EINVAL;
			}
			*off_count = 0;
			memset(prop_count, 0, sizeof(int) * prop_size);
			return rc;
		}
	}

	for (i = 0; i < prop_size; i++) {
		switch (dpu_prop[i].type) {
		case PROP_TYPE_U32:
			rc = of_property_read_u32(np, dpu_prop[i].prop_name,
				&val);
			break;
		case PROP_TYPE_U32_ARRAY:
			prop_count[i] = of_property_count_u32_elems(np,
				dpu_prop[i].prop_name);
			if (prop_count[i] < 0)
				rc = prop_count[i];
			break;
		case PROP_TYPE_STRING_ARRAY:
			prop_count[i] = of_property_count_strings(np,
				dpu_prop[i].prop_name);
			if (prop_count[i] < 0)
				rc = prop_count[i];
			break;
		case PROP_TYPE_BIT_OFFSET_ARRAY:
			of_get_property(np, dpu_prop[i].prop_name, &val);
			prop_count[i] = val / (MAX_BIT_OFFSET * sizeof(u32));
			break;
		case PROP_TYPE_NODE:
			snp = of_get_child_by_name(np,
					dpu_prop[i].prop_name);
			if (!snp)
				rc = -EINVAL;
			break;
		default:
			DPU_DEBUG("invalid property type:%d\n",
							dpu_prop[i].type);
			break;
		}
		DPU_DEBUG(
			"prop id:%d prop name:%s prop type:%d prop_count:%d\n",
			i, dpu_prop[i].prop_name,
			dpu_prop[i].type, prop_count[i]);

		if (rc && dpu_prop[i].is_mandatory &&
		   ((dpu_prop[i].type == PROP_TYPE_U32) ||
		    (dpu_prop[i].type == PROP_TYPE_NODE))) {
			DPU_ERROR("prop:%s not present\n",
						dpu_prop[i].prop_name);
			goto end;
		} else if (dpu_prop[i].type == PROP_TYPE_U32 ||
			dpu_prop[i].type == PROP_TYPE_BOOL ||
			dpu_prop[i].type == PROP_TYPE_NODE) {
			rc = 0;
			continue;
		}

		if (off_count && (prop_count[i] != *off_count) &&
				dpu_prop[i].is_mandatory) {
			DPU_ERROR(
				"prop:%s count:%d is different compared to offset array:%d\n",
				dpu_prop[i].prop_name,
				prop_count[i], *off_count);
			rc = -EINVAL;
			goto end;
		} else if (off_count && prop_count[i] != *off_count) {
			DPU_DEBUG(
				"prop:%s count:%d is different compared to offset array:%d\n",
				dpu_prop[i].prop_name,
				prop_count[i], *off_count);
			rc = 0;
			prop_count[i] = 0;
		}
		if (prop_count[i] < 0) {
			prop_count[i] = 0;
			if (dpu_prop[i].is_mandatory) {
				DPU_ERROR("prop:%s count:%d is negative\n",
					dpu_prop[i].prop_name, prop_count[i]);
				rc = -EINVAL;
			} else {
				rc = 0;
				DPU_DEBUG("prop:%s count:%d is negative\n",
					dpu_prop[i].prop_name, prop_count[i]);
			}
		}
	}

end:
	return rc;
}

static int _read_dt_entry(struct device_node *np,
	struct dpu_prop_type *dpu_prop, u32 prop_size, int *prop_count,
	bool *prop_exists,
	struct dpu_prop_value *prop_value)
{
	int rc = 0, i, j;

	for (i = 0; i < prop_size; i++) {
		prop_exists[i] = true;
		switch (dpu_prop[i].type) {
		case PROP_TYPE_U32:
			rc = of_property_read_u32(np, dpu_prop[i].prop_name,
				&PROP_VALUE_ACCESS(prop_value, i, 0));
			DPU_DEBUG(
				"prop id:%d prop name:%s prop type:%d value:0x%x\n",
				i, dpu_prop[i].prop_name,
				dpu_prop[i].type,
				PROP_VALUE_ACCESS(prop_value, i, 0));
			if (rc)
				prop_exists[i] = false;
			break;
		case PROP_TYPE_BOOL:
			PROP_VALUE_ACCESS(prop_value, i, 0) =
				of_property_read_bool(np,
					dpu_prop[i].prop_name);
			DPU_DEBUG(
				"prop id:%d prop name:%s prop type:%d value:0x%x\n",
				i, dpu_prop[i].prop_name,
				dpu_prop[i].type,
				PROP_VALUE_ACCESS(prop_value, i, 0));
			break;
		case PROP_TYPE_U32_ARRAY:
			rc = _parse_dt_u32_handler(np, dpu_prop[i].prop_name,
				&PROP_VALUE_ACCESS(prop_value, i, 0),
				prop_count[i], dpu_prop[i].is_mandatory);
			if (rc && dpu_prop[i].is_mandatory) {
				DPU_ERROR(
					"%s prop validation success but read failed\n",
					dpu_prop[i].prop_name);
				prop_exists[i] = false;
				goto end;
			} else {
				if (rc)
					prop_exists[i] = false;
				/* only for debug purpose */
				DPU_DEBUG("prop id:%d prop name:%s prop type:%d"
					, i, dpu_prop[i].prop_name,
					dpu_prop[i].type);
				for (j = 0; j < prop_count[i]; j++)
					DPU_DEBUG(" value[%d]:0x%x ", j,
						PROP_VALUE_ACCESS(prop_value, i,
								j));
				DPU_DEBUG("\n");
			}
			break;
		case PROP_TYPE_BIT_OFFSET_ARRAY:
			rc = _parse_dt_bit_offset(np, dpu_prop[i].prop_name,
				prop_value, i, prop_count[i],
				dpu_prop[i].is_mandatory);
			if (rc && dpu_prop[i].is_mandatory) {
				DPU_ERROR(
					"%s prop validation success but read failed\n",
					dpu_prop[i].prop_name);
				prop_exists[i] = false;
				goto end;
			} else {
				if (rc)
					prop_exists[i] = false;
				DPU_DEBUG(
					"prop id:%d prop name:%s prop type:%d",
					i, dpu_prop[i].prop_name,
					dpu_prop[i].type);
				for (j = 0; j < prop_count[i]; j++)
					DPU_DEBUG(
					"count[%d]: bit:0x%x off:0x%x\n", j,
					PROP_BITVALUE_ACCESS(prop_value,
						i, j, 0),
					PROP_BITVALUE_ACCESS(prop_value,
						i, j, 1));
				DPU_DEBUG("\n");
			}
			break;
		case PROP_TYPE_NODE:
			/* Node will be parsed in calling function */
			rc = 0;
			break;
		default:
			DPU_DEBUG("invalid property type:%d\n",
							dpu_prop[i].type);
			break;
		}
		rc = 0;
	}

end:
	return rc;
}

static void _dpu_sspp_setup_vig(struct dpu_mdss_cfg *dpu_cfg,
	struct dpu_sspp_cfg *sspp, struct dpu_sspp_sub_blks *sblk,
	bool *prop_exists, struct dpu_prop_value *prop_value, u32 *vig_count)
{
	sblk->maxupscale = MAX_UPSCALE_RATIO;
	sblk->maxdwnscale = MAX_DOWNSCALE_RATIO;
	sspp->id = SSPP_VIG0 + *vig_count;
	snprintf(sspp->name, DPU_HW_BLK_NAME_LEN, "sspp_%u",
			sspp->id - SSPP_VIG0);
	sspp->clk_ctrl = DPU_CLK_CTRL_VIG0 + *vig_count;
	sspp->type = SSPP_TYPE_VIG;
	set_bit(DPU_SSPP_QOS, &sspp->features);
	if (dpu_cfg->vbif_qos_nlvl == 8)
		set_bit(DPU_SSPP_QOS_8LVL, &sspp->features);
	(*vig_count)++;

	if (!prop_value)
		return;

	if (dpu_cfg->qseed_type == DPU_SSPP_SCALER_QSEED2) {
		set_bit(DPU_SSPP_SCALER_QSEED2, &sspp->features);
		sblk->scaler_blk.id = DPU_SSPP_SCALER_QSEED2;
		sblk->scaler_blk.base = PROP_VALUE_ACCESS(prop_value,
			VIG_QSEED_OFF, 0);
		sblk->scaler_blk.len = PROP_VALUE_ACCESS(prop_value,
			VIG_QSEED_LEN, 0);
		snprintf(sblk->scaler_blk.name, DPU_HW_BLK_NAME_LEN,
				"sspp_scaler%u", sspp->id - SSPP_VIG0);
	} else if (dpu_cfg->qseed_type == DPU_SSPP_SCALER_QSEED3) {
		set_bit(DPU_SSPP_SCALER_QSEED3, &sspp->features);
		sblk->scaler_blk.id = DPU_SSPP_SCALER_QSEED3;
		sblk->scaler_blk.base = PROP_VALUE_ACCESS(prop_value,
			VIG_QSEED_OFF, 0);
		sblk->scaler_blk.len = PROP_VALUE_ACCESS(prop_value,
			VIG_QSEED_LEN, 0);
		snprintf(sblk->scaler_blk.name, DPU_HW_BLK_NAME_LEN,
			"sspp_scaler%u", sspp->id - SSPP_VIG0);
	}

	if (dpu_cfg->has_sbuf)
		set_bit(DPU_SSPP_SBUF, &sspp->features);

	sblk->csc_blk.id = DPU_SSPP_CSC;
	snprintf(sblk->csc_blk.name, DPU_HW_BLK_NAME_LEN,
			"sspp_csc%u", sspp->id - SSPP_VIG0);
	if (dpu_cfg->csc_type == DPU_SSPP_CSC) {
		set_bit(DPU_SSPP_CSC, &sspp->features);
		sblk->csc_blk.base = PROP_VALUE_ACCESS(prop_value,
							VIG_CSC_OFF, 0);
	} else if (dpu_cfg->csc_type == DPU_SSPP_CSC_10BIT) {
		set_bit(DPU_SSPP_CSC_10BIT, &sspp->features);
		sblk->csc_blk.base = PROP_VALUE_ACCESS(prop_value,
							VIG_CSC_OFF, 0);
	}

	sblk->hsic_blk.id = DPU_SSPP_HSIC;
	snprintf(sblk->hsic_blk.name, DPU_HW_BLK_NAME_LEN,
			"sspp_hsic%u", sspp->id - SSPP_VIG0);
	if (prop_exists[VIG_HSIC_PROP]) {
		sblk->hsic_blk.base = PROP_VALUE_ACCESS(prop_value,
			VIG_HSIC_PROP, 0);
		sblk->hsic_blk.version = PROP_VALUE_ACCESS(prop_value,
			VIG_HSIC_PROP, 1);
		sblk->hsic_blk.len = 0;
		set_bit(DPU_SSPP_HSIC, &sspp->features);
	}

	sblk->memcolor_blk.id = DPU_SSPP_MEMCOLOR;
	snprintf(sblk->memcolor_blk.name, DPU_HW_BLK_NAME_LEN,
			"sspp_memcolor%u", sspp->id - SSPP_VIG0);
	if (prop_exists[VIG_MEMCOLOR_PROP]) {
		sblk->memcolor_blk.base = PROP_VALUE_ACCESS(prop_value,
			VIG_MEMCOLOR_PROP, 0);
		sblk->memcolor_blk.version = PROP_VALUE_ACCESS(prop_value,
			VIG_MEMCOLOR_PROP, 1);
		sblk->memcolor_blk.len = 0;
		set_bit(DPU_SSPP_MEMCOLOR, &sspp->features);
	}

	sblk->pcc_blk.id = DPU_SSPP_PCC;
	snprintf(sblk->pcc_blk.name, DPU_HW_BLK_NAME_LEN,
			"sspp_pcc%u", sspp->id - SSPP_VIG0);
	if (prop_exists[VIG_PCC_PROP]) {
		sblk->pcc_blk.base = PROP_VALUE_ACCESS(prop_value,
			VIG_PCC_PROP, 0);
		sblk->pcc_blk.version = PROP_VALUE_ACCESS(prop_value,
			VIG_PCC_PROP, 1);
		sblk->pcc_blk.len = 0;
		set_bit(DPU_SSPP_PCC, &sspp->features);
	}

	sblk->format_list = dpu_cfg->vig_formats;
	sblk->virt_format_list = dpu_cfg->dma_formats;
}

static void _dpu_sspp_setup_rgb(struct dpu_mdss_cfg *dpu_cfg,
	struct dpu_sspp_cfg *sspp, struct dpu_sspp_sub_blks *sblk,
	bool *prop_exists, struct dpu_prop_value *prop_value, u32 *rgb_count)
{
	sblk->maxupscale = MAX_UPSCALE_RATIO;
	sblk->maxdwnscale = MAX_DOWNSCALE_RATIO;
	sspp->id = SSPP_RGB0 + *rgb_count;
	snprintf(sspp->name, DPU_HW_BLK_NAME_LEN, "sspp_%u",
			sspp->id - SSPP_VIG0);
	sspp->clk_ctrl = DPU_CLK_CTRL_RGB0 + *rgb_count;
	sspp->type = SSPP_TYPE_RGB;
	set_bit(DPU_SSPP_QOS, &sspp->features);
	if (dpu_cfg->vbif_qos_nlvl == 8)
		set_bit(DPU_SSPP_QOS_8LVL, &sspp->features);
	(*rgb_count)++;

	if (!prop_value)
		return;

	if (dpu_cfg->qseed_type == DPU_SSPP_SCALER_QSEED2) {
		set_bit(DPU_SSPP_SCALER_RGB, &sspp->features);
		sblk->scaler_blk.id = DPU_SSPP_SCALER_QSEED2;
		sblk->scaler_blk.base = PROP_VALUE_ACCESS(prop_value,
			RGB_SCALER_OFF, 0);
		sblk->scaler_blk.len = PROP_VALUE_ACCESS(prop_value,
			RGB_SCALER_LEN, 0);
		snprintf(sblk->scaler_blk.name, DPU_HW_BLK_NAME_LEN,
			"sspp_scaler%u", sspp->id - SSPP_VIG0);
	} else if (dpu_cfg->qseed_type == DPU_SSPP_SCALER_QSEED3) {
		set_bit(DPU_SSPP_SCALER_RGB, &sspp->features);
		sblk->scaler_blk.id = DPU_SSPP_SCALER_QSEED3;
		sblk->scaler_blk.base = PROP_VALUE_ACCESS(prop_value,
			RGB_SCALER_LEN, 0);
		sblk->scaler_blk.len = PROP_VALUE_ACCESS(prop_value,
			SSPP_SCALE_SIZE, 0);
		snprintf(sblk->scaler_blk.name, DPU_HW_BLK_NAME_LEN,
			"sspp_scaler%u", sspp->id - SSPP_VIG0);
	}

	sblk->pcc_blk.id = DPU_SSPP_PCC;
	if (prop_exists[RGB_PCC_PROP]) {
		sblk->pcc_blk.base = PROP_VALUE_ACCESS(prop_value,
			RGB_PCC_PROP, 0);
		sblk->pcc_blk.version = PROP_VALUE_ACCESS(prop_value,
			RGB_PCC_PROP, 1);
		sblk->pcc_blk.len = 0;
		set_bit(DPU_SSPP_PCC, &sspp->features);
	}

	sblk->format_list = dpu_cfg->dma_formats;
	sblk->virt_format_list = NULL;
}

static void _dpu_sspp_setup_cursor(struct dpu_mdss_cfg *dpu_cfg,
	struct dpu_sspp_cfg *sspp, struct dpu_sspp_sub_blks *sblk,
	struct dpu_prop_value *prop_value, u32 *cursor_count)
{
	if (!IS_DPU_MAJOR_MINOR_SAME(dpu_cfg->hwversion, DPU_HW_VER_300))
		DPU_ERROR("invalid sspp type %d, xin id %d\n",
				sspp->type, sspp->xin_id);
	set_bit(DPU_SSPP_CURSOR, &sspp->features);
	sblk->maxupscale = SSPP_UNITY_SCALE;
	sblk->maxdwnscale = SSPP_UNITY_SCALE;
	sblk->format_list = dpu_cfg->cursor_formats;
	sblk->virt_format_list = NULL;
	sspp->id = SSPP_CURSOR0 + *cursor_count;
	snprintf(sspp->name, DPU_HW_BLK_NAME_LEN, "sspp_%u",
			sspp->id - SSPP_VIG0);
	sspp->clk_ctrl = DPU_CLK_CTRL_CURSOR0 + *cursor_count;
	sspp->type = SSPP_TYPE_CURSOR;
	(*cursor_count)++;
}

static void _dpu_sspp_setup_dma(struct dpu_mdss_cfg *dpu_cfg,
	struct dpu_sspp_cfg *sspp, struct dpu_sspp_sub_blks *sblk,
	struct dpu_prop_value *prop_value, u32 *dma_count)
{
	sblk->maxupscale = SSPP_UNITY_SCALE;
	sblk->maxdwnscale = SSPP_UNITY_SCALE;
	sblk->format_list = dpu_cfg->dma_formats;
	sblk->virt_format_list = dpu_cfg->dma_formats;
	sspp->id = SSPP_DMA0 + *dma_count;
	sspp->clk_ctrl = DPU_CLK_CTRL_DMA0 + *dma_count;
	snprintf(sspp->name, DPU_HW_BLK_NAME_LEN, "sspp_%u",
			sspp->id - SSPP_VIG0);
	sspp->type = SSPP_TYPE_DMA;
	set_bit(DPU_SSPP_QOS, &sspp->features);
	if (dpu_cfg->vbif_qos_nlvl == 8)
		set_bit(DPU_SSPP_QOS_8LVL, &sspp->features);
	(*dma_count)++;
}

static int dpu_sspp_parse_dt(struct device_node *np,
	struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[SSPP_PROP_MAX], off_count, i, j;
	int vig_prop_count[VIG_PROP_MAX], rgb_prop_count[RGB_PROP_MAX];
	bool prop_exists[SSPP_PROP_MAX], vig_prop_exists[VIG_PROP_MAX];
	bool rgb_prop_exists[RGB_PROP_MAX];
	struct dpu_prop_value *prop_value = NULL;
	struct dpu_prop_value *vig_prop_value = NULL, *rgb_prop_value = NULL;
	const char *type;
	struct dpu_sspp_cfg *sspp;
	struct dpu_sspp_sub_blks *sblk;
	u32 vig_count = 0, dma_count = 0, rgb_count = 0, cursor_count = 0;
	struct device_node *snp = NULL;

	prop_value = kcalloc(SSPP_PROP_MAX,
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, sspp_prop, ARRAY_SIZE(sspp_prop),
		prop_count, &off_count);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, sspp_prop, ARRAY_SIZE(sspp_prop), prop_count,
					prop_exists, prop_value);
	if (rc)
		goto end;

	dpu_cfg->sspp_count = off_count;

	/* get vig feature dt properties if they exist */
	snp = of_get_child_by_name(np, sspp_prop[SSPP_VIG_BLOCKS].prop_name);
	if (snp) {
		vig_prop_value = kcalloc(VIG_PROP_MAX,
			sizeof(struct dpu_prop_value), GFP_KERNEL);
		if (!vig_prop_value) {
			rc = -ENOMEM;
			goto end;
		}
		rc = _validate_dt_entry(snp, vig_prop, ARRAY_SIZE(vig_prop),
			vig_prop_count, NULL);
		if (rc)
			goto end;
		rc = _read_dt_entry(snp, vig_prop, ARRAY_SIZE(vig_prop),
				vig_prop_count, vig_prop_exists,
				vig_prop_value);
	}

	/* get rgb feature dt properties if they exist */
	snp = of_get_child_by_name(np, sspp_prop[SSPP_RGB_BLOCKS].prop_name);
	if (snp) {
		rgb_prop_value = kcalloc(RGB_PROP_MAX,
					sizeof(struct dpu_prop_value),
					GFP_KERNEL);
		if (!rgb_prop_value) {
			rc = -ENOMEM;
			goto end;
		}
		rc = _validate_dt_entry(snp, rgb_prop, ARRAY_SIZE(rgb_prop),
			rgb_prop_count, NULL);
		if (rc)
			goto end;
		rc = _read_dt_entry(snp, rgb_prop, ARRAY_SIZE(rgb_prop),
				rgb_prop_count, rgb_prop_exists,
				rgb_prop_value);
	}

	for (i = 0; i < off_count; i++) {
		sspp = dpu_cfg->sspp + i;
		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		sspp->sblk = sblk;

		sspp->base = PROP_VALUE_ACCESS(prop_value, SSPP_OFF, i);
		sspp->len = PROP_VALUE_ACCESS(prop_value, SSPP_SIZE, 0);
		sblk->maxlinewidth = dpu_cfg->max_sspp_linewidth;

		set_bit(DPU_SSPP_SRC, &sspp->features);

		if (dpu_cfg->has_cdp)
			set_bit(DPU_SSPP_CDP, &sspp->features);

		if (dpu_cfg->ts_prefill_rev == 1) {
			set_bit(DPU_SSPP_TS_PREFILL, &sspp->features);
		} else if (dpu_cfg->ts_prefill_rev == 2) {
			set_bit(DPU_SSPP_TS_PREFILL, &sspp->features);
			set_bit(DPU_SSPP_TS_PREFILL_REC1, &sspp->features);
		}

		sblk->smart_dma_priority =
			PROP_VALUE_ACCESS(prop_value, SSPP_SMART_DMA, i);

		if (sblk->smart_dma_priority && dpu_cfg->smart_dma_rev)
			set_bit(dpu_cfg->smart_dma_rev, &sspp->features);

		sblk->src_blk.id = DPU_SSPP_SRC;

		of_property_read_string_index(np,
				sspp_prop[SSPP_TYPE].prop_name, i, &type);
		if (!strcmp(type, "vig")) {
			_dpu_sspp_setup_vig(dpu_cfg, sspp, sblk,
				vig_prop_exists, vig_prop_value, &vig_count);
		} else if (!strcmp(type, "rgb")) {
			_dpu_sspp_setup_rgb(dpu_cfg, sspp, sblk,
				rgb_prop_exists, rgb_prop_value, &rgb_count);
		} else if (!strcmp(type, "cursor")) {
			/* No prop values for cursor pipes */
			_dpu_sspp_setup_cursor(dpu_cfg, sspp, sblk, NULL,
								&cursor_count);
		} else if (!strcmp(type, "dma")) {
			/* No prop values for DMA pipes */
			_dpu_sspp_setup_dma(dpu_cfg, sspp, sblk, NULL,
								&dma_count);
		} else {
			DPU_ERROR("invalid sspp type:%s\n", type);
			rc = -EINVAL;
			goto end;
		}

		snprintf(sblk->src_blk.name, DPU_HW_BLK_NAME_LEN, "sspp_src_%u",
				sspp->id - SSPP_VIG0);

		if (sspp->clk_ctrl >= DPU_CLK_CTRL_MAX) {
			DPU_ERROR("%s: invalid clk ctrl: %d\n",
					sblk->src_blk.name, sspp->clk_ctrl);
			rc = -EINVAL;
			goto end;
		}

		sblk->maxhdeciexp = MAX_HORZ_DECIMATION;
		sblk->maxvdeciexp = MAX_VERT_DECIMATION;

		sspp->xin_id = PROP_VALUE_ACCESS(prop_value, SSPP_XIN, i);
		sblk->pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE;
		sblk->src_blk.len = PROP_VALUE_ACCESS(prop_value, SSPP_SIZE, 0);

		if (PROP_VALUE_ACCESS(prop_value, SSPP_EXCL_RECT, i) == 1)
			set_bit(DPU_SSPP_EXCL_RECT, &sspp->features);

		if (prop_exists[SSPP_MAX_PER_PIPE_BW])
			sblk->max_per_pipe_bw = PROP_VALUE_ACCESS(prop_value,
					SSPP_MAX_PER_PIPE_BW, i);
		else
			sblk->max_per_pipe_bw = DEFAULT_MAX_PER_PIPE_BW;

		for (j = 0; j < dpu_cfg->mdp_count; j++) {
			dpu_cfg->mdp[j].clk_ctrls[sspp->clk_ctrl].reg_off =
				PROP_BITVALUE_ACCESS(prop_value,
						SSPP_CLK_CTRL, i, 0);
			dpu_cfg->mdp[j].clk_ctrls[sspp->clk_ctrl].bit_off =
				PROP_BITVALUE_ACCESS(prop_value,
						SSPP_CLK_CTRL, i, 1);
		}

		DPU_DEBUG(
			"xin:%d ram:%d clk%d:%x/%d\n",
			sspp->xin_id,
			sblk->pixel_ram_size,
			sspp->clk_ctrl,
			dpu_cfg->mdp[0].clk_ctrls[sspp->clk_ctrl].reg_off,
			dpu_cfg->mdp[0].clk_ctrls[sspp->clk_ctrl].bit_off);
	}

end:
	kfree(prop_value);
	kfree(vig_prop_value);
	kfree(rgb_prop_value);
	return rc;
}

static int dpu_ctl_parse_dt(struct device_node *np,
		struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[HW_PROP_MAX], i;
	bool prop_exists[HW_PROP_MAX];
	struct dpu_prop_value *prop_value = NULL;
	struct dpu_ctl_cfg *ctl;
	u32 off_count;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument input param\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(HW_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, ctl_prop, ARRAY_SIZE(ctl_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	dpu_cfg->ctl_count = off_count;

	rc = _read_dt_entry(np, ctl_prop, ARRAY_SIZE(ctl_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		ctl = dpu_cfg->ctl + i;
		ctl->base = PROP_VALUE_ACCESS(prop_value, HW_OFF, i);
		ctl->len = PROP_VALUE_ACCESS(prop_value, HW_LEN, 0);
		ctl->id = CTL_0 + i;
		snprintf(ctl->name, DPU_HW_BLK_NAME_LEN, "ctl_%u",
				ctl->id - CTL_0);

		if (i < MAX_SPLIT_DISPLAY_CTL)
			set_bit(DPU_CTL_SPLIT_DISPLAY, &ctl->features);
		if (i < MAX_PP_SPLIT_DISPLAY_CTL)
			set_bit(DPU_CTL_PINGPONG_SPLIT, &ctl->features);
		if (dpu_cfg->has_sbuf)
			set_bit(DPU_CTL_SBUF, &ctl->features);
	}

end:
	kfree(prop_value);
	return rc;
}

static int dpu_mixer_parse_dt(struct device_node *np,
						struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[MIXER_PROP_MAX], i, j;
	int blocks_prop_count[MIXER_BLOCKS_PROP_MAX];
	int blend_prop_count[MIXER_BLEND_PROP_MAX];
	bool prop_exists[MIXER_PROP_MAX];
	bool blocks_prop_exists[MIXER_BLOCKS_PROP_MAX];
	bool blend_prop_exists[MIXER_BLEND_PROP_MAX];
	struct dpu_prop_value *prop_value = NULL, *blocks_prop_value = NULL;
	struct dpu_prop_value *blend_prop_value = NULL;
	u32 off_count, blend_off_count, max_blendstages, lm_pair_mask;
	struct dpu_lm_cfg *mixer;
	struct dpu_lm_sub_blks *sblk;
	int pp_count, dspp_count, ds_count;
	u32 pp_idx, dspp_idx, ds_idx;
	struct device_node *snp = NULL;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument input param\n");
		rc = -EINVAL;
		goto end;
	}
	max_blendstages = dpu_cfg->max_mixer_blendstages;

	prop_value = kcalloc(MIXER_PROP_MAX,
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, mixer_prop, ARRAY_SIZE(mixer_prop),
		prop_count, &off_count);
	if (rc)
		goto end;

	dpu_cfg->mixer_count = off_count;

	rc = _read_dt_entry(np, mixer_prop, ARRAY_SIZE(mixer_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	pp_count = dpu_cfg->pingpong_count;
	dspp_count = dpu_cfg->dspp_count;
	ds_count = dpu_cfg->ds_count;

	/* get mixer feature dt properties if they exist */
	snp = of_get_child_by_name(np, mixer_prop[MIXER_BLOCKS].prop_name);
	if (snp) {
		blocks_prop_value = kzalloc(MIXER_BLOCKS_PROP_MAX *
				MAX_DPU_HW_BLK * sizeof(struct dpu_prop_value),
				GFP_KERNEL);
		if (!blocks_prop_value) {
			rc = -ENOMEM;
			goto end;
		}
		rc = _validate_dt_entry(snp, mixer_blocks_prop,
			ARRAY_SIZE(mixer_blocks_prop), blocks_prop_count, NULL);
		if (rc)
			goto end;
		rc = _read_dt_entry(snp, mixer_blocks_prop,
				ARRAY_SIZE(mixer_blocks_prop),
				blocks_prop_count, blocks_prop_exists,
				blocks_prop_value);
	}

	/* get the blend_op register offsets */
	blend_prop_value = kzalloc(MIXER_BLEND_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!blend_prop_value) {
		rc = -ENOMEM;
		goto end;
	}
	rc = _validate_dt_entry(np, mixer_blend_prop,
		ARRAY_SIZE(mixer_blend_prop), blend_prop_count,
		&blend_off_count);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, mixer_blend_prop, ARRAY_SIZE(mixer_blend_prop),
		blend_prop_count, blend_prop_exists, blend_prop_value);
	if (rc)
		goto end;

	for (i = 0, pp_idx = 0, dspp_idx = 0, ds_idx = 0; i < off_count; i++) {
		mixer = dpu_cfg->mixer + i;
		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		mixer->sblk = sblk;

		mixer->base = PROP_VALUE_ACCESS(prop_value, MIXER_OFF, i);
		mixer->len = PROP_VALUE_ACCESS(prop_value, MIXER_LEN, 0);
		mixer->id = LM_0 + i;
		snprintf(mixer->name, DPU_HW_BLK_NAME_LEN, "lm_%u",
				mixer->id - LM_0);

		if (!prop_exists[MIXER_LEN])
			mixer->len = DEFAULT_DPU_HW_BLOCK_LEN;

		lm_pair_mask = PROP_VALUE_ACCESS(prop_value,
				MIXER_PAIR_MASK, i);
		if (lm_pair_mask)
			mixer->lm_pair_mask = 1 << lm_pair_mask;

		sblk->maxblendstages = max_blendstages;
		sblk->maxwidth = dpu_cfg->max_mixer_width;

		for (j = 0; j < blend_off_count; j++)
			sblk->blendstage_base[j] =
				PROP_VALUE_ACCESS(blend_prop_value,
						MIXER_BLEND_OP_OFF, j);

		if (dpu_cfg->has_src_split)
			set_bit(DPU_MIXER_SOURCESPLIT, &mixer->features);
		if (dpu_cfg->has_dim_layer)
			set_bit(DPU_DIM_LAYER, &mixer->features);

		if ((i < ROT_LM_OFFSET) || (i >= LINE_LM_OFFSET)) {
			mixer->pingpong = pp_count > 0 ? pp_idx + PINGPONG_0
								: PINGPONG_MAX;
			mixer->dspp = dspp_count > 0 ? dspp_idx + DSPP_0
								: DSPP_MAX;
			mixer->ds = ds_count > 0 ? ds_idx + DS_0 : DS_MAX;
			pp_count--;
			dspp_count--;
			ds_count--;
			pp_idx++;
			dspp_idx++;
			ds_idx++;
		} else {
			mixer->pingpong = PINGPONG_MAX;
			mixer->dspp = DSPP_MAX;
			mixer->ds = DS_MAX;
		}

		sblk->gc.id = DPU_MIXER_GC;
		if (blocks_prop_value && blocks_prop_exists[MIXER_GC_PROP]) {
			sblk->gc.base = PROP_VALUE_ACCESS(blocks_prop_value,
					MIXER_GC_PROP, 0);
			sblk->gc.version = PROP_VALUE_ACCESS(blocks_prop_value,
					MIXER_GC_PROP, 1);
			sblk->gc.len = 0;
			set_bit(DPU_MIXER_GC, &mixer->features);
		}
	}

end:
	kfree(prop_value);
	kfree(blocks_prop_value);
	kfree(blend_prop_value);
	return rc;
}

static int dpu_intf_parse_dt(struct device_node *np,
						struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[INTF_PROP_MAX], i;
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[INTF_PROP_MAX];
	u32 off_count;
	u32 dsi_count = 0, none_count = 0, hdmi_count = 0, dp_count = 0;
	const char *type;
	struct dpu_intf_cfg *intf;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(INTF_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, intf_prop, ARRAY_SIZE(intf_prop),
		prop_count, &off_count);
	if (rc)
		goto end;

	dpu_cfg->intf_count = off_count;

	rc = _read_dt_entry(np, intf_prop, ARRAY_SIZE(intf_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		intf = dpu_cfg->intf + i;
		intf->base = PROP_VALUE_ACCESS(prop_value, INTF_OFF, i);
		intf->len = PROP_VALUE_ACCESS(prop_value, INTF_LEN, 0);
		intf->id = INTF_0 + i;
		snprintf(intf->name, DPU_HW_BLK_NAME_LEN, "intf_%u",
				intf->id - INTF_0);

		if (!prop_exists[INTF_LEN])
			intf->len = DEFAULT_DPU_HW_BLOCK_LEN;

		intf->prog_fetch_lines_worst_case =
				!prop_exists[INTF_PREFETCH] ?
				dpu_cfg->perf.min_prefill_lines :
				PROP_VALUE_ACCESS(prop_value, INTF_PREFETCH, i);

		of_property_read_string_index(np,
				intf_prop[INTF_TYPE].prop_name, i, &type);
		if (!strcmp(type, "dsi")) {
			intf->type = INTF_DSI;
			intf->controller_id = dsi_count;
			dsi_count++;
		} else if (!strcmp(type, "hdmi")) {
			intf->type = INTF_HDMI;
			intf->controller_id = hdmi_count;
			hdmi_count++;
		} else if (!strcmp(type, "dp")) {
			intf->type = INTF_DP;
			intf->controller_id = dp_count;
			dp_count++;
		} else {
			intf->type = INTF_NONE;
			intf->controller_id = none_count;
			none_count++;
		}

		if (dpu_cfg->has_sbuf)
			set_bit(DPU_INTF_ROT_START, &intf->features);
	}

end:
	kfree(prop_value);
	return rc;
}

static int dpu_wb_parse_dt(struct device_node *np, struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[WB_PROP_MAX], i, j;
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[WB_PROP_MAX];
	u32 off_count;
	struct dpu_wb_cfg *wb;
	struct dpu_wb_sub_blocks *sblk;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(WB_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, wb_prop, ARRAY_SIZE(wb_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	dpu_cfg->wb_count = off_count;

	rc = _read_dt_entry(np, wb_prop, ARRAY_SIZE(wb_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		wb = dpu_cfg->wb + i;
		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		wb->sblk = sblk;

		wb->base = PROP_VALUE_ACCESS(prop_value, WB_OFF, i);
		wb->id = WB_0 + PROP_VALUE_ACCESS(prop_value, WB_ID, i);
		snprintf(wb->name, DPU_HW_BLK_NAME_LEN, "wb_%u",
				wb->id - WB_0);
		wb->clk_ctrl = DPU_CLK_CTRL_WB0 +
			PROP_VALUE_ACCESS(prop_value, WB_ID, i);
		wb->xin_id = PROP_VALUE_ACCESS(prop_value, WB_XIN_ID, i);

		if (wb->clk_ctrl >= DPU_CLK_CTRL_MAX) {
			DPU_ERROR("%s: invalid clk ctrl: %d\n",
					wb->name, wb->clk_ctrl);
			rc = -EINVAL;
			goto end;
		}

		if (IS_DPU_MAJOR_MINOR_SAME((dpu_cfg->hwversion),
				DPU_HW_VER_170))
			wb->vbif_idx = VBIF_NRT;
		else
			wb->vbif_idx = VBIF_RT;

		wb->len = PROP_VALUE_ACCESS(prop_value, WB_LEN, 0);
		if (!prop_exists[WB_LEN])
			wb->len = DEFAULT_DPU_HW_BLOCK_LEN;
		sblk->maxlinewidth = dpu_cfg->max_wb_linewidth;

		if (wb->id >= LINE_MODE_WB_OFFSET)
			set_bit(DPU_WB_LINE_MODE, &wb->features);
		else
			set_bit(DPU_WB_BLOCK_MODE, &wb->features);
		set_bit(DPU_WB_TRAFFIC_SHAPER, &wb->features);
		set_bit(DPU_WB_YUV_CONFIG, &wb->features);

		if (dpu_cfg->has_cdp)
			set_bit(DPU_WB_CDP, &wb->features);

		set_bit(DPU_WB_QOS, &wb->features);
		if (dpu_cfg->vbif_qos_nlvl == 8)
			set_bit(DPU_WB_QOS_8LVL, &wb->features);

		if (dpu_cfg->has_wb_ubwc)
			set_bit(DPU_WB_UBWC, &wb->features);

		for (j = 0; j < dpu_cfg->mdp_count; j++) {
			dpu_cfg->mdp[j].clk_ctrls[wb->clk_ctrl].reg_off =
				PROP_BITVALUE_ACCESS(prop_value,
						WB_CLK_CTRL, i, 0);
			dpu_cfg->mdp[j].clk_ctrls[wb->clk_ctrl].bit_off =
				PROP_BITVALUE_ACCESS(prop_value,
						WB_CLK_CTRL, i, 1);
		}

		wb->format_list = dpu_cfg->wb_formats;

		DPU_DEBUG(
			"wb:%d xin:%d vbif:%d clk%d:%x/%d\n",
			wb->id - WB_0,
			wb->xin_id,
			wb->vbif_idx,
			wb->clk_ctrl,
			dpu_cfg->mdp[0].clk_ctrls[wb->clk_ctrl].reg_off,
			dpu_cfg->mdp[0].clk_ctrls[wb->clk_ctrl].bit_off);
	}

end:
	kfree(prop_value);
	return rc;
}

static void _dpu_dspp_setup_blocks(struct dpu_mdss_cfg *dpu_cfg,
	struct dpu_dspp_cfg *dspp, struct dpu_dspp_sub_blks *sblk,
	bool *prop_exists, struct dpu_prop_value *prop_value)
{
	sblk->igc.id = DPU_DSPP_IGC;
	if (prop_exists[DSPP_IGC_PROP]) {
		sblk->igc.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_IGC_PROP, 0);
		sblk->igc.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_IGC_PROP, 1);
		sblk->igc.len = 0;
		set_bit(DPU_DSPP_IGC, &dspp->features);
	}

	sblk->pcc.id = DPU_DSPP_PCC;
	if (prop_exists[DSPP_PCC_PROP]) {
		sblk->pcc.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_PCC_PROP, 0);
		sblk->pcc.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_PCC_PROP, 1);
		sblk->pcc.len = 0;
		set_bit(DPU_DSPP_PCC, &dspp->features);
	}

	sblk->gc.id = DPU_DSPP_GC;
	if (prop_exists[DSPP_GC_PROP]) {
		sblk->gc.base = PROP_VALUE_ACCESS(prop_value, DSPP_GC_PROP, 0);
		sblk->gc.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_GC_PROP, 1);
		sblk->gc.len = 0;
		set_bit(DPU_DSPP_GC, &dspp->features);
	}

	sblk->gamut.id = DPU_DSPP_GAMUT;
	if (prop_exists[DSPP_GAMUT_PROP]) {
		sblk->gamut.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_GAMUT_PROP, 0);
		sblk->gamut.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_GAMUT_PROP, 1);
		sblk->gamut.len = 0;
		set_bit(DPU_DSPP_GAMUT, &dspp->features);
	}

	sblk->dither.id = DPU_DSPP_DITHER;
	if (prop_exists[DSPP_DITHER_PROP]) {
		sblk->dither.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_DITHER_PROP, 0);
		sblk->dither.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_DITHER_PROP, 1);
		sblk->dither.len = 0;
		set_bit(DPU_DSPP_DITHER, &dspp->features);
	}

	sblk->hist.id = DPU_DSPP_HIST;
	if (prop_exists[DSPP_HIST_PROP]) {
		sblk->hist.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_HIST_PROP, 0);
		sblk->hist.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_HIST_PROP, 1);
		sblk->hist.len = 0;
		set_bit(DPU_DSPP_HIST, &dspp->features);
	}

	sblk->hsic.id = DPU_DSPP_HSIC;
	if (prop_exists[DSPP_HSIC_PROP]) {
		sblk->hsic.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_HSIC_PROP, 0);
		sblk->hsic.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_HSIC_PROP, 1);
		sblk->hsic.len = 0;
		set_bit(DPU_DSPP_HSIC, &dspp->features);
	}

	sblk->memcolor.id = DPU_DSPP_MEMCOLOR;
	if (prop_exists[DSPP_MEMCOLOR_PROP]) {
		sblk->memcolor.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_MEMCOLOR_PROP, 0);
		sblk->memcolor.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_MEMCOLOR_PROP, 1);
		sblk->memcolor.len = 0;
		set_bit(DPU_DSPP_MEMCOLOR, &dspp->features);
	}

	sblk->sixzone.id = DPU_DSPP_SIXZONE;
	if (prop_exists[DSPP_SIXZONE_PROP]) {
		sblk->sixzone.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_SIXZONE_PROP, 0);
		sblk->sixzone.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_SIXZONE_PROP, 1);
		sblk->sixzone.len = 0;
		set_bit(DPU_DSPP_SIXZONE, &dspp->features);
	}

	sblk->vlut.id = DPU_DSPP_VLUT;
	if (prop_exists[DSPP_VLUT_PROP]) {
		sblk->vlut.base = PROP_VALUE_ACCESS(prop_value,
			DSPP_VLUT_PROP, 0);
		sblk->vlut.version = PROP_VALUE_ACCESS(prop_value,
			DSPP_VLUT_PROP, 1);
		sblk->sixzone.len = 0;
		set_bit(DPU_DSPP_VLUT, &dspp->features);
	}
}

#if IS_ENABLED(CONFIG_DRM_MSM_ROTATOR)
static struct dpu_prop_type inline_rot_prop[INLINE_ROT_PROP_MAX] = {
	{INLINE_ROT_XIN, "qcom,dpu-inline-rot-xin", false,
							PROP_TYPE_U32_ARRAY},
	{INLINE_ROT_XIN_TYPE, "qcom,dpu-inline-rot-xin-type", false,
							PROP_TYPE_STRING_ARRAY},
	{INLINE_ROT_CLK_CTRL, "qcom,dpu-inline-rot-clk-ctrl", false,
						PROP_TYPE_BIT_OFFSET_ARRAY},
};

static void _dpu_inline_rot_parse_dt(struct device_node *np,
		struct dpu_mdss_cfg *dpu_cfg, struct dpu_rot_cfg *rot)
{
	int rc, prop_count[INLINE_ROT_PROP_MAX], i, j, index;
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[INLINE_ROT_PROP_MAX];
	u32 off_count, sspp_count = 0, wb_count = 0;
	const char *type;

	prop_value = kcalloc(INLINE_ROT_PROP_MAX,
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value)
		return;

	rc = _validate_dt_entry(np, inline_rot_prop,
			ARRAY_SIZE(inline_rot_prop), prop_count, &off_count);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, inline_rot_prop, ARRAY_SIZE(inline_rot_prop),
			prop_count, prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		rot->vbif_cfg[i].xin_id = PROP_VALUE_ACCESS(prop_value,
							INLINE_ROT_XIN, i);
		of_property_read_string_index(np,
				inline_rot_prop[INLINE_ROT_XIN_TYPE].prop_name,
				i, &type);

		if (!strcmp(type, "sspp")) {
			rot->vbif_cfg[i].num = INLINE_ROT0_SSPP + sspp_count;
			rot->vbif_cfg[i].is_read = true;
			rot->vbif_cfg[i].clk_ctrl =
					DPU_CLK_CTRL_INLINE_ROT0_SSPP
					+ sspp_count;
			sspp_count++;
		} else if (!strcmp(type, "wb")) {
			rot->vbif_cfg[i].num = INLINE_ROT0_WB + wb_count;
			rot->vbif_cfg[i].is_read = false;
			rot->vbif_cfg[i].clk_ctrl =
					DPU_CLK_CTRL_INLINE_ROT0_WB
					+ wb_count;
			wb_count++;
		} else {
			DPU_ERROR("invalid rotator vbif type:%s\n", type);
			goto end;
		}

		index = rot->vbif_cfg[i].clk_ctrl;
		if (index < 0 || index >= DPU_CLK_CTRL_MAX) {
			DPU_ERROR("invalid clk_ctrl enum:%d\n", index);
			goto end;
		}

		for (j = 0; j < dpu_cfg->mdp_count; j++) {
			dpu_cfg->mdp[j].clk_ctrls[index].reg_off =
				PROP_BITVALUE_ACCESS(prop_value,
						INLINE_ROT_CLK_CTRL, i, 0);
			dpu_cfg->mdp[j].clk_ctrls[index].bit_off =
				PROP_BITVALUE_ACCESS(prop_value,
						INLINE_ROT_CLK_CTRL, i, 1);
		}

		DPU_DEBUG("rot- xin:%d, num:%d, rd:%d, clk:%d:0x%x/%d\n",
				rot->vbif_cfg[i].xin_id,
				rot->vbif_cfg[i].num,
				rot->vbif_cfg[i].is_read,
				rot->vbif_cfg[i].clk_ctrl,
				dpu_cfg->mdp[0].clk_ctrls[index].reg_off,
				dpu_cfg->mdp[0].clk_ctrls[index].bit_off);
	}

	rot->vbif_idx = VBIF_RT;
	rot->xin_count = off_count;

end:
	kfree(prop_value);
}
#endif

static int dpu_rot_parse_dt(struct device_node *np,
		struct dpu_mdss_cfg *dpu_cfg)
{
#if IS_ENABLED(CONFIG_DRM_MSM_ROTATOR)
	struct dpu_rot_cfg *rot;
	struct platform_device *pdev;
	struct of_phandle_args phargs;
	struct llcc_slice_desc *slice;
	int rc = 0, i;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	for (i = 0; i < ROT_MAX; i++) {
		rot = dpu_cfg->rot + dpu_cfg->rot_count;
		rot->base = 0;
		rot->len = 0;

		rc = of_parse_phandle_with_args(np,
				"qcom,dpu-inline-rotator", "#list-cells",
				i, &phargs);
		if (rc) {
			rc = 0;
			break;
		} else if (!phargs.np || !phargs.args_count) {
			rc = -EINVAL;
			break;
		}

		rot->id = ROT_0 + phargs.args[0];

		pdev = of_find_device_by_node(phargs.np);
		if (pdev) {
			slice = llcc_slice_getd(&pdev->dev, "rotator");
			if (IS_ERR_OR_NULL(slice)) {
				rot->pdev = NULL;
				DPU_ERROR("failed to get system cache %ld\n",
						PTR_ERR(slice));
			} else {
				rot->scid = llcc_get_slice_id(slice);
				rot->slice_size = llcc_get_slice_size(slice);
				rot->pdev = pdev;
				llcc_slice_putd(slice);
				DPU_DEBUG("rot:%d scid:%d slice_size:%zukb\n",
						rot->id, rot->scid,
						rot->slice_size);
				_dpu_inline_rot_parse_dt(np, dpu_cfg, rot);
				dpu_cfg->rot_count++;
			}
		} else {
			rot->pdev = NULL;
			DPU_ERROR("invalid dpu rotator node\n");
		}

		of_node_put(phargs.np);
	}

	if (dpu_cfg->rot_count) {
		dpu_cfg->has_sbuf = true;
		dpu_cfg->sbuf_headroom = DEFAULT_SBUF_HEADROOM;
	}

end:
	return rc;
#else
	return 0;
#endif
}

static int dpu_dspp_top_parse_dt(struct device_node *np,
		struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[DSPP_TOP_PROP_MAX];
	bool prop_exists[DSPP_TOP_PROP_MAX];
	struct dpu_prop_value *prop_value = NULL;
	u32 off_count;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(DSPP_TOP_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, dspp_top_prop, ARRAY_SIZE(dspp_top_prop),
		prop_count, &off_count);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, dspp_top_prop, ARRAY_SIZE(dspp_top_prop),
		prop_count, prop_exists, prop_value);
	if (rc)
		goto end;

	if (off_count != 1) {
		DPU_ERROR("invalid dspp_top off_count:%d\n", off_count);
		rc = -EINVAL;
		goto end;
	}

	dpu_cfg->dspp_top.base =
		PROP_VALUE_ACCESS(prop_value, DSPP_TOP_OFF, 0);
	dpu_cfg->dspp_top.len =
		PROP_VALUE_ACCESS(prop_value, DSPP_TOP_SIZE, 0);
	snprintf(dpu_cfg->dspp_top.name, DPU_HW_BLK_NAME_LEN, "dspp_top");

end:
	kfree(prop_value);
	return rc;
}

static int dpu_dspp_parse_dt(struct device_node *np,
						struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[DSPP_PROP_MAX], i;
	int ad_prop_count[AD_PROP_MAX];
	bool prop_exists[DSPP_PROP_MAX], ad_prop_exists[AD_PROP_MAX];
	bool blocks_prop_exists[DSPP_BLOCKS_PROP_MAX];
	struct dpu_prop_value *ad_prop_value = NULL;
	int blocks_prop_count[DSPP_BLOCKS_PROP_MAX];
	struct dpu_prop_value *prop_value = NULL, *blocks_prop_value = NULL;
	u32 off_count, ad_off_count;
	struct dpu_dspp_cfg *dspp;
	struct dpu_dspp_sub_blks *sblk;
	struct device_node *snp = NULL;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(DSPP_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, dspp_prop, ARRAY_SIZE(dspp_prop),
		prop_count, &off_count);
	if (rc)
		goto end;

	dpu_cfg->dspp_count = off_count;

	rc = _read_dt_entry(np, dspp_prop, ARRAY_SIZE(dspp_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	/* Parse AD dtsi entries */
	ad_prop_value = kcalloc(AD_PROP_MAX,
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!ad_prop_value) {
		rc = -ENOMEM;
		goto end;
	}
	rc = _validate_dt_entry(np, ad_prop, ARRAY_SIZE(ad_prop),
		ad_prop_count, &ad_off_count);
	if (rc)
		goto end;
	rc = _read_dt_entry(np, ad_prop, ARRAY_SIZE(ad_prop), ad_prop_count,
		ad_prop_exists, ad_prop_value);
	if (rc)
		goto end;

	/* get DSPP feature dt properties if they exist */
	snp = of_get_child_by_name(np, dspp_prop[DSPP_BLOCKS].prop_name);
	if (snp) {
		blocks_prop_value = kzalloc(DSPP_BLOCKS_PROP_MAX *
				MAX_DPU_HW_BLK * sizeof(struct dpu_prop_value),
				GFP_KERNEL);
		if (!blocks_prop_value) {
			rc = -ENOMEM;
			goto end;
		}
		rc = _validate_dt_entry(snp, dspp_blocks_prop,
			ARRAY_SIZE(dspp_blocks_prop), blocks_prop_count, NULL);
		if (rc)
			goto end;
		rc = _read_dt_entry(snp, dspp_blocks_prop,
			ARRAY_SIZE(dspp_blocks_prop), blocks_prop_count,
			blocks_prop_exists, blocks_prop_value);
		if (rc)
			goto end;
	}

	for (i = 0; i < off_count; i++) {
		dspp = dpu_cfg->dspp + i;
		dspp->base = PROP_VALUE_ACCESS(prop_value, DSPP_OFF, i);
		dspp->len = PROP_VALUE_ACCESS(prop_value, DSPP_SIZE, 0);
		dspp->id = DSPP_0 + i;
		snprintf(dspp->name, DPU_HW_BLK_NAME_LEN, "dspp_%u",
				dspp->id - DSPP_0);

		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		dspp->sblk = sblk;

		if (blocks_prop_value)
			_dpu_dspp_setup_blocks(dpu_cfg, dspp, sblk,
					blocks_prop_exists, blocks_prop_value);

		sblk->ad.id = DPU_DSPP_AD;
		dpu_cfg->ad_count = ad_off_count;
		if (ad_prop_value && (i < ad_off_count) &&
		    ad_prop_exists[AD_OFF]) {
			sblk->ad.base = PROP_VALUE_ACCESS(ad_prop_value,
				AD_OFF, i);
			sblk->ad.version = PROP_VALUE_ACCESS(ad_prop_value,
				AD_VERSION, 0);
			set_bit(DPU_DSPP_AD, &dspp->features);
		}
	}

end:
	kfree(prop_value);
	kfree(ad_prop_value);
	kfree(blocks_prop_value);
	return rc;
}

static int dpu_ds_parse_dt(struct device_node *np,
			struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[DS_PROP_MAX], top_prop_count[DS_TOP_PROP_MAX], i;
	struct dpu_prop_value *prop_value = NULL, *top_prop_value = NULL;
	bool prop_exists[DS_PROP_MAX], top_prop_exists[DS_TOP_PROP_MAX];
	u32 off_count = 0, top_off_count = 0;
	struct dpu_ds_cfg *ds;
	struct dpu_ds_top_cfg *ds_top = NULL;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	if (!dpu_cfg->mdp[0].has_dest_scaler) {
		DPU_DEBUG("dest scaler feature not supported\n");
		rc = 0;
		goto end;
	}

	/* Parse the dest scaler top register offset and capabilities */
	top_prop_value = kzalloc(DS_TOP_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!top_prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, ds_top_prop,
				ARRAY_SIZE(ds_top_prop),
				top_prop_count, &top_off_count);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, ds_top_prop,
			ARRAY_SIZE(ds_top_prop), top_prop_count,
			top_prop_exists, top_prop_value);
	if (rc)
		goto end;

	/* Parse the offset of each dest scaler block */
	prop_value = kcalloc(DS_PROP_MAX,
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, ds_prop, ARRAY_SIZE(ds_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	dpu_cfg->ds_count = off_count;

	rc = _read_dt_entry(np, ds_prop, ARRAY_SIZE(ds_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	if (!off_count)
		goto end;

	ds_top = kzalloc(sizeof(struct dpu_ds_top_cfg), GFP_KERNEL);
	if (!ds_top) {
		rc = -ENOMEM;
		goto end;
	}

	ds_top->id = DS_TOP;
	snprintf(ds_top->name, DPU_HW_BLK_NAME_LEN, "ds_top_%u",
		ds_top->id - DS_TOP);
	ds_top->base = PROP_VALUE_ACCESS(top_prop_value, DS_TOP_OFF, 0);
	ds_top->len = PROP_VALUE_ACCESS(top_prop_value, DS_TOP_LEN, 0);
	ds_top->maxupscale = MAX_UPSCALE_RATIO;

	ds_top->maxinputwidth = PROP_VALUE_ACCESS(top_prop_value,
			DS_TOP_INPUT_LINEWIDTH, 0);
	if (!top_prop_exists[DS_TOP_INPUT_LINEWIDTH])
		ds_top->maxinputwidth = DEFAULT_DPU_LINE_WIDTH;

	ds_top->maxoutputwidth = PROP_VALUE_ACCESS(top_prop_value,
			DS_TOP_OUTPUT_LINEWIDTH, 0);
	if (!top_prop_exists[DS_TOP_OUTPUT_LINEWIDTH])
		ds_top->maxoutputwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH;

	for (i = 0; i < off_count; i++) {
		ds = dpu_cfg->ds + i;
		ds->top = ds_top;
		ds->base = PROP_VALUE_ACCESS(prop_value, DS_OFF, i);
		ds->id = DS_0 + i;
		ds->len = PROP_VALUE_ACCESS(prop_value, DS_LEN, 0);
		snprintf(ds->name, DPU_HW_BLK_NAME_LEN, "ds_%u",
			ds->id - DS_0);

		if (!prop_exists[DS_LEN])
			ds->len = DEFAULT_DPU_HW_BLOCK_LEN;

		if (dpu_cfg->qseed_type == DPU_SSPP_SCALER_QSEED3)
			set_bit(DPU_SSPP_SCALER_QSEED3, &ds->features);
	}

end:
	kfree(top_prop_value);
	kfree(prop_value);
	return rc;
};

static int dpu_dsc_parse_dt(struct device_node *np,
			struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[MAX_BLOCKS], i;
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[DSC_PROP_MAX];
	u32 off_count;
	struct dpu_dsc_cfg *dsc;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(DSC_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, dsc_prop, ARRAY_SIZE(dsc_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	dpu_cfg->dsc_count = off_count;

	rc = _read_dt_entry(np, dsc_prop, ARRAY_SIZE(dsc_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		dsc = dpu_cfg->dsc + i;
		dsc->base = PROP_VALUE_ACCESS(prop_value, DSC_OFF, i);
		dsc->id = DSC_0 + i;
		dsc->len = PROP_VALUE_ACCESS(prop_value, DSC_LEN, 0);
		snprintf(dsc->name, DPU_HW_BLK_NAME_LEN, "dsc_%u",
				dsc->id - DSC_0);

		if (!prop_exists[DSC_LEN])
			dsc->len = DEFAULT_DPU_HW_BLOCK_LEN;
	}

end:
	kfree(prop_value);
	return rc;
};

static int dpu_cdm_parse_dt(struct device_node *np,
				struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[HW_PROP_MAX], i;
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[HW_PROP_MAX];
	u32 off_count;
	struct dpu_cdm_cfg *cdm;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(HW_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, cdm_prop, ARRAY_SIZE(cdm_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	dpu_cfg->cdm_count = off_count;

	rc = _read_dt_entry(np, cdm_prop, ARRAY_SIZE(cdm_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		cdm = dpu_cfg->cdm + i;
		cdm->base = PROP_VALUE_ACCESS(prop_value, HW_OFF, i);
		cdm->id = CDM_0 + i;
		snprintf(cdm->name, DPU_HW_BLK_NAME_LEN, "cdm_%u",
				cdm->id - CDM_0);
		cdm->len = PROP_VALUE_ACCESS(prop_value, HW_LEN, 0);

		/* intf3 and wb2 for cdm block */
		cdm->wb_connect = dpu_cfg->wb_count ? BIT(WB_2) : BIT(31);
		cdm->intf_connect = dpu_cfg->intf_count ? BIT(INTF_3) : BIT(31);
	}

end:
	kfree(prop_value);
	return rc;
}

static int dpu_vbif_parse_dt(struct device_node *np,
				struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[VBIF_PROP_MAX], i, j, k;
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[VBIF_PROP_MAX];
	u32 off_count, vbif_len;
	struct dpu_vbif_cfg *vbif;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(VBIF_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, vbif_prop, ARRAY_SIZE(vbif_prop),
			prop_count, &off_count);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_DYNAMIC_OT_RD_LIMIT], 1,
			&prop_count[VBIF_DYNAMIC_OT_RD_LIMIT], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_DYNAMIC_OT_WR_LIMIT], 1,
			&prop_count[VBIF_DYNAMIC_OT_WR_LIMIT], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_RT_REMAP], 1,
			&prop_count[VBIF_QOS_RT_REMAP], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_NRT_REMAP], 1,
			&prop_count[VBIF_QOS_NRT_REMAP], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_MEMTYPE_0], 1,
			&prop_count[VBIF_MEMTYPE_0], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_MEMTYPE_1], 1,
			&prop_count[VBIF_MEMTYPE_1], NULL);
	if (rc)
		goto end;

	dpu_cfg->vbif_count = off_count;

	rc = _read_dt_entry(np, vbif_prop, ARRAY_SIZE(vbif_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	vbif_len = PROP_VALUE_ACCESS(prop_value, VBIF_LEN, 0);
	if (!prop_exists[VBIF_LEN])
		vbif_len = DEFAULT_DPU_HW_BLOCK_LEN;

	for (i = 0; i < off_count; i++) {
		vbif = dpu_cfg->vbif + i;
		vbif->base = PROP_VALUE_ACCESS(prop_value, VBIF_OFF, i);
		vbif->len = vbif_len;
		vbif->id = VBIF_0 + PROP_VALUE_ACCESS(prop_value, VBIF_ID, i);
		snprintf(vbif->name, DPU_HW_BLK_NAME_LEN, "vbif_%u",
				vbif->id - VBIF_0);

		DPU_DEBUG("vbif:%d\n", vbif->id - VBIF_0);

		vbif->xin_halt_timeout = VBIF_XIN_HALT_TIMEOUT;

		vbif->default_ot_rd_limit = PROP_VALUE_ACCESS(prop_value,
				VBIF_DEFAULT_OT_RD_LIMIT, 0);
		DPU_DEBUG("default_ot_rd_limit=%u\n",
				vbif->default_ot_rd_limit);

		vbif->default_ot_wr_limit = PROP_VALUE_ACCESS(prop_value,
				VBIF_DEFAULT_OT_WR_LIMIT, 0);
		DPU_DEBUG("default_ot_wr_limit=%u\n",
				vbif->default_ot_wr_limit);

		vbif->dynamic_ot_rd_tbl.count =
				prop_count[VBIF_DYNAMIC_OT_RD_LIMIT] / 2;
		DPU_DEBUG("dynamic_ot_rd_tbl.count=%u\n",
				vbif->dynamic_ot_rd_tbl.count);
		if (vbif->dynamic_ot_rd_tbl.count) {
			vbif->dynamic_ot_rd_tbl.cfg = kcalloc(
				vbif->dynamic_ot_rd_tbl.count,
				sizeof(struct dpu_vbif_dynamic_ot_cfg),
				GFP_KERNEL);
			if (!vbif->dynamic_ot_rd_tbl.cfg) {
				rc = -ENOMEM;
				goto end;
			}
		}

		for (j = 0, k = 0; j < vbif->dynamic_ot_rd_tbl.count; j++) {
			vbif->dynamic_ot_rd_tbl.cfg[j].pps = (u64)
				PROP_VALUE_ACCESS(prop_value,
				VBIF_DYNAMIC_OT_RD_LIMIT, k++);
			vbif->dynamic_ot_rd_tbl.cfg[j].ot_limit =
				PROP_VALUE_ACCESS(prop_value,
				VBIF_DYNAMIC_OT_RD_LIMIT, k++);
			DPU_DEBUG("dynamic_ot_rd_tbl[%d].cfg=<%llu %u>\n", j,
				vbif->dynamic_ot_rd_tbl.cfg[j].pps,
				vbif->dynamic_ot_rd_tbl.cfg[j].ot_limit);
		}

		vbif->dynamic_ot_wr_tbl.count =
				prop_count[VBIF_DYNAMIC_OT_WR_LIMIT] / 2;
		DPU_DEBUG("dynamic_ot_wr_tbl.count=%u\n",
				vbif->dynamic_ot_wr_tbl.count);
		if (vbif->dynamic_ot_wr_tbl.count) {
			vbif->dynamic_ot_wr_tbl.cfg = kcalloc(
				vbif->dynamic_ot_wr_tbl.count,
				sizeof(struct dpu_vbif_dynamic_ot_cfg),
				GFP_KERNEL);
			if (!vbif->dynamic_ot_wr_tbl.cfg) {
				rc = -ENOMEM;
				goto end;
			}
		}

		for (j = 0, k = 0; j < vbif->dynamic_ot_wr_tbl.count; j++) {
			vbif->dynamic_ot_wr_tbl.cfg[j].pps = (u64)
				PROP_VALUE_ACCESS(prop_value,
				VBIF_DYNAMIC_OT_WR_LIMIT, k++);
			vbif->dynamic_ot_wr_tbl.cfg[j].ot_limit =
				PROP_VALUE_ACCESS(prop_value,
				VBIF_DYNAMIC_OT_WR_LIMIT, k++);
			DPU_DEBUG("dynamic_ot_wr_tbl[%d].cfg=<%llu %u>\n", j,
				vbif->dynamic_ot_wr_tbl.cfg[j].pps,
				vbif->dynamic_ot_wr_tbl.cfg[j].ot_limit);
		}

		if (vbif->default_ot_rd_limit || vbif->default_ot_wr_limit ||
				vbif->dynamic_ot_rd_tbl.count ||
				vbif->dynamic_ot_wr_tbl.count)
			set_bit(DPU_VBIF_QOS_OTLIM, &vbif->features);

		vbif->qos_rt_tbl.npriority_lvl =
				prop_count[VBIF_QOS_RT_REMAP];
		DPU_DEBUG("qos_rt_tbl.npriority_lvl=%u\n",
				vbif->qos_rt_tbl.npriority_lvl);
		if (vbif->qos_rt_tbl.npriority_lvl == dpu_cfg->vbif_qos_nlvl) {
			vbif->qos_rt_tbl.priority_lvl = kcalloc(
				vbif->qos_rt_tbl.npriority_lvl, sizeof(u32),
				GFP_KERNEL);
			if (!vbif->qos_rt_tbl.priority_lvl) {
				rc = -ENOMEM;
				goto end;
			}
		} else if (vbif->qos_rt_tbl.npriority_lvl) {
			vbif->qos_rt_tbl.npriority_lvl = 0;
			vbif->qos_rt_tbl.priority_lvl = NULL;
			DPU_ERROR("invalid qos rt table\n");
		}

		for (j = 0; j < vbif->qos_rt_tbl.npriority_lvl; j++) {
			vbif->qos_rt_tbl.priority_lvl[j] =
				PROP_VALUE_ACCESS(prop_value,
						VBIF_QOS_RT_REMAP, j);
			DPU_DEBUG("lvl[%d]=%u\n", j,
					vbif->qos_rt_tbl.priority_lvl[j]);
		}

		vbif->qos_nrt_tbl.npriority_lvl =
				prop_count[VBIF_QOS_NRT_REMAP];
		DPU_DEBUG("qos_nrt_tbl.npriority_lvl=%u\n",
				vbif->qos_nrt_tbl.npriority_lvl);

		if (vbif->qos_nrt_tbl.npriority_lvl == dpu_cfg->vbif_qos_nlvl) {
			vbif->qos_nrt_tbl.priority_lvl = kcalloc(
				vbif->qos_nrt_tbl.npriority_lvl, sizeof(u32),
				GFP_KERNEL);
			if (!vbif->qos_nrt_tbl.priority_lvl) {
				rc = -ENOMEM;
				goto end;
			}
		} else if (vbif->qos_nrt_tbl.npriority_lvl) {
			vbif->qos_nrt_tbl.npriority_lvl = 0;
			vbif->qos_nrt_tbl.priority_lvl = NULL;
			DPU_ERROR("invalid qos nrt table\n");
		}

		for (j = 0; j < vbif->qos_nrt_tbl.npriority_lvl; j++) {
			vbif->qos_nrt_tbl.priority_lvl[j] =
				PROP_VALUE_ACCESS(prop_value,
						VBIF_QOS_NRT_REMAP, j);
			DPU_DEBUG("lvl[%d]=%u\n", j,
					vbif->qos_nrt_tbl.priority_lvl[j]);
		}

		if (vbif->qos_rt_tbl.npriority_lvl ||
				vbif->qos_nrt_tbl.npriority_lvl)
			set_bit(DPU_VBIF_QOS_REMAP, &vbif->features);

		vbif->memtype_count = prop_count[VBIF_MEMTYPE_0] +
					prop_count[VBIF_MEMTYPE_1];
		if (vbif->memtype_count > MAX_XIN_COUNT) {
			vbif->memtype_count = 0;
			DPU_ERROR("too many memtype defs, ignoring entries\n");
		}
		for (j = 0, k = 0; j < prop_count[VBIF_MEMTYPE_0]; j++)
			vbif->memtype[k++] = PROP_VALUE_ACCESS(
					prop_value, VBIF_MEMTYPE_0, j);
		for (j = 0; j < prop_count[VBIF_MEMTYPE_1]; j++)
			vbif->memtype[k++] = PROP_VALUE_ACCESS(
					prop_value, VBIF_MEMTYPE_1, j);
	}

end:
	kfree(prop_value);
	return rc;
}

static int dpu_pp_parse_dt(struct device_node *np, struct dpu_mdss_cfg *dpu_cfg)
{
	int rc, prop_count[PP_PROP_MAX], i;
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[PP_PROP_MAX];
	u32 off_count;
	struct dpu_pingpong_cfg *pp;
	struct dpu_pingpong_sub_blks *sblk;

	if (!dpu_cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(PP_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, pp_prop, ARRAY_SIZE(pp_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	dpu_cfg->pingpong_count = off_count;

	rc = _read_dt_entry(np, pp_prop, ARRAY_SIZE(pp_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		pp = dpu_cfg->pingpong + i;
		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		pp->sblk = sblk;

		pp->base = PROP_VALUE_ACCESS(prop_value, PP_OFF, i);
		pp->id = PINGPONG_0 + i;
		snprintf(pp->name, DPU_HW_BLK_NAME_LEN, "pingpong_%u",
				pp->id - PINGPONG_0);
		pp->len = PROP_VALUE_ACCESS(prop_value, PP_LEN, 0);

		sblk->te.base = PROP_VALUE_ACCESS(prop_value, TE_OFF, i);
		sblk->te.id = DPU_PINGPONG_TE;
		snprintf(sblk->te.name, DPU_HW_BLK_NAME_LEN, "te_%u",
				pp->id - PINGPONG_0);
		set_bit(DPU_PINGPONG_TE, &pp->features);

		sblk->te2.base = PROP_VALUE_ACCESS(prop_value, TE2_OFF, i);
		if (sblk->te2.base) {
			sblk->te2.id = DPU_PINGPONG_TE2;
			snprintf(sblk->te2.name, DPU_HW_BLK_NAME_LEN, "te2_%u",
					pp->id - PINGPONG_0);
			set_bit(DPU_PINGPONG_TE2, &pp->features);
			set_bit(DPU_PINGPONG_SPLIT, &pp->features);
		}

		if (PROP_VALUE_ACCESS(prop_value, PP_SLAVE, i))
			set_bit(DPU_PINGPONG_SLAVE, &pp->features);

		sblk->dsc.base = PROP_VALUE_ACCESS(prop_value, DSC_OFF, i);
		if (sblk->dsc.base) {
			sblk->dsc.id = DPU_PINGPONG_DSC;
			snprintf(sblk->dsc.name, DPU_HW_BLK_NAME_LEN, "dsc_%u",
					pp->id - PINGPONG_0);
			set_bit(DPU_PINGPONG_DSC, &pp->features);
		}

		sblk->dither.base = PROP_VALUE_ACCESS(prop_value, DITHER_OFF,
							i);
		if (sblk->dither.base) {
			sblk->dither.id = DPU_PINGPONG_DITHER;
			snprintf(sblk->dither.name, DPU_HW_BLK_NAME_LEN,
					"dither_%u", pp->id);
			set_bit(DPU_PINGPONG_DITHER, &pp->features);
		}
		sblk->dither.len = PROP_VALUE_ACCESS(prop_value, DITHER_LEN, 0);
		sblk->dither.version = PROP_VALUE_ACCESS(prop_value, DITHER_VER,
								0);
	}

end:
	kfree(prop_value);
	return rc;
}

static int dpu_parse_dt(struct device_node *np, struct dpu_mdss_cfg *cfg)
{
	int rc, dma_rc, len, prop_count[DPU_PROP_MAX];
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[DPU_PROP_MAX];
	const char *type;

	if (!cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(DPU_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, dpu_prop, ARRAY_SIZE(dpu_prop), prop_count,
		&len);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, dpu_prop, ARRAY_SIZE(dpu_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	cfg->mdss_count = 1;
	cfg->mdss[0].base = MDSS_BASE_OFFSET;
	cfg->mdss[0].id = MDP_TOP;
	snprintf(cfg->mdss[0].name, DPU_HW_BLK_NAME_LEN, "mdss_%u",
			cfg->mdss[0].id - MDP_TOP);

	cfg->mdp_count = 1;
	cfg->mdp[0].id = MDP_TOP;
	snprintf(cfg->mdp[0].name, DPU_HW_BLK_NAME_LEN, "top_%u",
		cfg->mdp[0].id - MDP_TOP);
	cfg->mdp[0].base = PROP_VALUE_ACCESS(prop_value, DPU_OFF, 0);
	cfg->mdp[0].len = PROP_VALUE_ACCESS(prop_value, DPU_LEN, 0);
	if (!prop_exists[DPU_LEN])
		cfg->mdp[0].len = DEFAULT_DPU_HW_BLOCK_LEN;

	cfg->max_sspp_linewidth = PROP_VALUE_ACCESS(prop_value,
			SSPP_LINEWIDTH, 0);
	if (!prop_exists[SSPP_LINEWIDTH])
		cfg->max_sspp_linewidth = DEFAULT_DPU_LINE_WIDTH;

	cfg->max_mixer_width = PROP_VALUE_ACCESS(prop_value,
			MIXER_LINEWIDTH, 0);
	if (!prop_exists[MIXER_LINEWIDTH])
		cfg->max_mixer_width = DEFAULT_DPU_LINE_WIDTH;

	cfg->max_mixer_blendstages = PROP_VALUE_ACCESS(prop_value,
			MIXER_BLEND, 0);
	if (!prop_exists[MIXER_BLEND])
		cfg->max_mixer_blendstages = DEFAULT_DPU_MIXER_BLENDSTAGES;

	cfg->max_wb_linewidth = PROP_VALUE_ACCESS(prop_value, WB_LINEWIDTH, 0);
	if (!prop_exists[WB_LINEWIDTH])
		cfg->max_wb_linewidth = DEFAULT_DPU_LINE_WIDTH;

	cfg->mdp[0].highest_bank_bit = PROP_VALUE_ACCESS(prop_value,
			BANK_BIT, 0);
	if (!prop_exists[BANK_BIT])
		cfg->mdp[0].highest_bank_bit = DEFAULT_DPU_HIGHEST_BANK_BIT;

	cfg->ubwc_version = PROP_VALUE_ACCESS(prop_value, UBWC_VERSION, 0);
	if (!prop_exists[UBWC_VERSION])
		cfg->ubwc_version = DEFAULT_DPU_UBWC_VERSION;

	cfg->mdp[0].ubwc_static = PROP_VALUE_ACCESS(prop_value, UBWC_STATIC, 0);
	if (!prop_exists[UBWC_STATIC])
		cfg->mdp[0].ubwc_static = DEFAULT_DPU_UBWC_STATIC;

	cfg->mdp[0].ubwc_swizzle = PROP_VALUE_ACCESS(prop_value,
			UBWC_SWIZZLE, 0);
	if (!prop_exists[UBWC_SWIZZLE])
		cfg->mdp[0].ubwc_swizzle = DEFAULT_DPU_UBWC_SWIZZLE;

	cfg->mdp[0].has_dest_scaler =
		PROP_VALUE_ACCESS(prop_value, DEST_SCALER, 0);

	rc = of_property_read_string(np, dpu_prop[QSEED_TYPE].prop_name, &type);
	if (!rc && !strcmp(type, "qseedv3")) {
		cfg->qseed_type = DPU_SSPP_SCALER_QSEED3;
	} else if (!rc && !strcmp(type, "qseedv2")) {
		cfg->qseed_type = DPU_SSPP_SCALER_QSEED2;
	} else if (rc) {
		DPU_DEBUG("invalid QSEED configuration\n");
		rc = 0;
	}

	rc = of_property_read_string(np, dpu_prop[CSC_TYPE].prop_name, &type);
	if (!rc && !strcmp(type, "csc")) {
		cfg->csc_type = DPU_SSPP_CSC;
	} else if (!rc && !strcmp(type, "csc-10bit")) {
		cfg->csc_type = DPU_SSPP_CSC_10BIT;
	} else if (rc) {
		DPU_DEBUG("invalid csc configuration\n");
		rc = 0;
	}

	/*
	 * Current DPU support only Smart DMA 2.0.
	 * No support for Smart DMA 1.0 yet.
	 */
	cfg->smart_dma_rev = 0;
	dma_rc = of_property_read_string(np, dpu_prop[SMART_DMA_REV].prop_name,
			&type);
	if (!dma_rc && !strcmp(type, "smart_dma_v2")) {
		cfg->smart_dma_rev = DPU_SSPP_SMART_DMA_V2;
	} else if (!dma_rc && !strcmp(type, "smart_dma_v1")) {
		DPU_ERROR("smart dma 1.0 is not supported in DPU\n");
		cfg->smart_dma_rev = 0;
	}

	cfg->has_src_split = PROP_VALUE_ACCESS(prop_value, SRC_SPLIT, 0);
	cfg->has_dim_layer = PROP_VALUE_ACCESS(prop_value, DIM_LAYER, 0);
	cfg->has_idle_pc = PROP_VALUE_ACCESS(prop_value, IDLE_PC, 0);
end:
	kfree(prop_value);
	return rc;
}

static int dpu_parse_reg_dma_dt(struct device_node *np,
		struct dpu_mdss_cfg *dpu_cfg)
{
	u32 val;
	int rc = 0;
	int i = 0;

	dpu_cfg->reg_dma_count = 0;
	for (i = 0; i < REG_DMA_PROP_MAX; i++) {
		rc = of_property_read_u32(np, reg_dma_prop[i].prop_name,
				&val);
		if (rc)
			break;
		switch (i) {
		case REG_DMA_OFF:
			dpu_cfg->dma_cfg.base = val;
			break;
		case REG_DMA_VERSION:
			dpu_cfg->dma_cfg.version = val;
			break;
		case REG_DMA_TRIGGER_OFF:
			dpu_cfg->dma_cfg.trigger_sel_off = val;
			break;
		default:
			break;
		}
	}
	if (!rc && i == REG_DMA_PROP_MAX)
		dpu_cfg->reg_dma_count = 1;
	/* reg dma is optional feature hence return 0 */
	return 0;
}

static int dpu_perf_parse_dt(struct device_node *np, struct dpu_mdss_cfg *cfg)
{
	int rc, len, prop_count[PERF_PROP_MAX];
	struct dpu_prop_value *prop_value = NULL;
	bool prop_exists[PERF_PROP_MAX];
	const char *str = NULL;
	int j, k;

	if (!cfg) {
		DPU_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(PERF_PROP_MAX *
			sizeof(struct dpu_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, dpu_perf_prop, ARRAY_SIZE(dpu_perf_prop),
			prop_count, &len);
	if (rc)
		goto freeprop;

	rc = _validate_dt_entry(np, &dpu_perf_prop[PERF_DANGER_LUT], 1,
			&prop_count[PERF_DANGER_LUT], NULL);
	if (rc)
		goto freeprop;

	rc = _validate_dt_entry(np, &dpu_perf_prop[PERF_SAFE_LUT], 1,
			&prop_count[PERF_SAFE_LUT], NULL);
	if (rc)
		goto freeprop;

	rc = _validate_dt_entry(np, &dpu_perf_prop[PERF_QOS_LUT_LINEAR], 1,
			&prop_count[PERF_QOS_LUT_LINEAR], NULL);
	if (rc)
		goto freeprop;

	rc = _validate_dt_entry(np, &dpu_perf_prop[PERF_QOS_LUT_MACROTILE], 1,
			&prop_count[PERF_QOS_LUT_MACROTILE], NULL);
	if (rc)
		goto freeprop;

	rc = _validate_dt_entry(np, &dpu_perf_prop[PERF_QOS_LUT_NRT], 1,
			&prop_count[PERF_QOS_LUT_NRT], NULL);
	if (rc)
		goto freeprop;

	rc = _validate_dt_entry(np, &dpu_perf_prop[PERF_QOS_LUT_CWB], 1,
			&prop_count[PERF_QOS_LUT_CWB], NULL);
	if (rc)
		goto freeprop;

	rc = _validate_dt_entry(np, &dpu_perf_prop[PERF_CDP_SETTING], 1,
			&prop_count[PERF_CDP_SETTING], NULL);
	if (rc)
		goto freeprop;

	rc = _read_dt_entry(np, dpu_perf_prop, ARRAY_SIZE(dpu_perf_prop),
			prop_count, prop_exists, prop_value);
	if (rc)
		goto freeprop;

	cfg->perf.max_bw_low =
			prop_exists[PERF_MAX_BW_LOW] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MAX_BW_LOW, 0) :
			DEFAULT_MAX_BW_LOW;
	cfg->perf.max_bw_high =
			prop_exists[PERF_MAX_BW_HIGH] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MAX_BW_HIGH, 0) :
			DEFAULT_MAX_BW_HIGH;
	cfg->perf.min_core_ib =
			prop_exists[PERF_MIN_CORE_IB] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MIN_CORE_IB, 0) :
			DEFAULT_MAX_BW_LOW;
	cfg->perf.min_llcc_ib =
			prop_exists[PERF_MIN_LLCC_IB] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MIN_LLCC_IB, 0) :
			DEFAULT_MAX_BW_LOW;
	cfg->perf.min_dram_ib =
			prop_exists[PERF_MIN_DRAM_IB] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MIN_DRAM_IB, 0) :
			DEFAULT_MAX_BW_LOW;

	/*
	 * The following performance parameters (e.g. core_ib_ff) are
	 * mapped directly as device tree string constants.
	 */
	rc = of_property_read_string(np,
			dpu_perf_prop[PERF_CORE_IB_FF].prop_name, &str);
	cfg->perf.core_ib_ff = rc ? DEFAULT_CORE_IB_FF : str;
	rc = of_property_read_string(np,
			dpu_perf_prop[PERF_CORE_CLK_FF].prop_name, &str);
	cfg->perf.core_clk_ff = rc ? DEFAULT_CORE_CLK_FF : str;
	rc = of_property_read_string(np,
			dpu_perf_prop[PERF_COMP_RATIO_RT].prop_name, &str);
	cfg->perf.comp_ratio_rt = rc ? DEFAULT_COMP_RATIO_RT : str;
	rc = of_property_read_string(np,
			dpu_perf_prop[PERF_COMP_RATIO_NRT].prop_name, &str);
	cfg->perf.comp_ratio_nrt = rc ? DEFAULT_COMP_RATIO_NRT : str;
	rc = 0;

	cfg->perf.undersized_prefill_lines =
			prop_exists[PERF_UNDERSIZED_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_UNDERSIZED_PREFILL_LINES, 0) :
			DEFAULT_UNDERSIZED_PREFILL_LINES;
	cfg->perf.xtra_prefill_lines =
			prop_exists[PERF_XTRA_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_XTRA_PREFILL_LINES, 0) :
			DEFAULT_XTRA_PREFILL_LINES;
	cfg->perf.dest_scale_prefill_lines =
			prop_exists[PERF_DEST_SCALE_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_DEST_SCALE_PREFILL_LINES, 0) :
			DEFAULT_DEST_SCALE_PREFILL_LINES;
	cfg->perf.macrotile_prefill_lines =
			prop_exists[PERF_MACROTILE_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_MACROTILE_PREFILL_LINES, 0) :
			DEFAULT_MACROTILE_PREFILL_LINES;
	cfg->perf.yuv_nv12_prefill_lines =
			prop_exists[PERF_YUV_NV12_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_YUV_NV12_PREFILL_LINES, 0) :
			DEFAULT_YUV_NV12_PREFILL_LINES;
	cfg->perf.linear_prefill_lines =
			prop_exists[PERF_LINEAR_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_LINEAR_PREFILL_LINES, 0) :
			DEFAULT_LINEAR_PREFILL_LINES;
	cfg->perf.downscaling_prefill_lines =
			prop_exists[PERF_DOWNSCALING_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_DOWNSCALING_PREFILL_LINES, 0) :
			DEFAULT_DOWNSCALING_PREFILL_LINES;
	cfg->perf.amortizable_threshold =
			prop_exists[PERF_AMORTIZABLE_THRESHOLD] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_AMORTIZABLE_THRESHOLD, 0) :
			DEFAULT_AMORTIZABLE_THRESHOLD;

	if (prop_exists[PERF_DANGER_LUT] && prop_count[PERF_DANGER_LUT] <=
			DPU_QOS_LUT_USAGE_MAX) {
		for (j = 0; j < prop_count[PERF_DANGER_LUT]; j++) {
			cfg->perf.danger_lut_tbl[j] =
					PROP_VALUE_ACCESS(prop_value,
						PERF_DANGER_LUT, j);
			DPU_DEBUG("danger usage:%d lut:0x%x\n",
					j, cfg->perf.danger_lut_tbl[j]);
		}
	}

	if (prop_exists[PERF_SAFE_LUT] && prop_count[PERF_SAFE_LUT] <=
			DPU_QOS_LUT_USAGE_MAX) {
		for (j = 0; j < prop_count[PERF_SAFE_LUT]; j++) {
			cfg->perf.safe_lut_tbl[j] =
					PROP_VALUE_ACCESS(prop_value,
						PERF_SAFE_LUT, j);
			DPU_DEBUG("safe usage:%d lut:0x%x\n",
					j, cfg->perf.safe_lut_tbl[j]);
		}
	}

	for (j = 0; j < DPU_QOS_LUT_USAGE_MAX; j++) {
		static const u32 prop_key[DPU_QOS_LUT_USAGE_MAX] = {
			[DPU_QOS_LUT_USAGE_LINEAR] =
					PERF_QOS_LUT_LINEAR,
			[DPU_QOS_LUT_USAGE_MACROTILE] =
					PERF_QOS_LUT_MACROTILE,
			[DPU_QOS_LUT_USAGE_NRT] =
					PERF_QOS_LUT_NRT,
			[DPU_QOS_LUT_USAGE_CWB] =
					PERF_QOS_LUT_CWB,
		};
		const u32 entry_size = 3;
		int m, count;
		int key = prop_key[j];

		if (!prop_exists[key])
			continue;

		count = prop_count[key] / entry_size;

		cfg->perf.qos_lut_tbl[j].entries = kcalloc(count,
			sizeof(struct dpu_qos_lut_entry), GFP_KERNEL);
		if (!cfg->perf.qos_lut_tbl[j].entries) {
			rc = -ENOMEM;
			goto freeprop;
		}

		for (k = 0, m = 0; k < count; k++, m += entry_size) {
			u64 lut_hi, lut_lo;

			cfg->perf.qos_lut_tbl[j].entries[k].fl =
					PROP_VALUE_ACCESS(prop_value, key, m);
			lut_hi = PROP_VALUE_ACCESS(prop_value, key, m + 1);
			lut_lo = PROP_VALUE_ACCESS(prop_value, key, m + 2);
			cfg->perf.qos_lut_tbl[j].entries[k].lut =
					(lut_hi << 32) | lut_lo;
			DPU_DEBUG("usage:%d.%d fl:%d lut:0x%llx\n",
				j, k,
				cfg->perf.qos_lut_tbl[j].entries[k].fl,
				cfg->perf.qos_lut_tbl[j].entries[k].lut);
		}
		cfg->perf.qos_lut_tbl[j].nentry = count;
	}

	if (prop_exists[PERF_CDP_SETTING]) {
		const u32 prop_size = 2;
		u32 count = prop_count[PERF_CDP_SETTING] / prop_size;

		count = min_t(u32, count, DPU_PERF_CDP_USAGE_MAX);

		for (j = 0; j < count; j++) {
			cfg->perf.cdp_cfg[j].rd_enable =
					PROP_VALUE_ACCESS(prop_value,
					PERF_CDP_SETTING, j * prop_size);
			cfg->perf.cdp_cfg[j].wr_enable =
					PROP_VALUE_ACCESS(prop_value,
					PERF_CDP_SETTING, j * prop_size + 1);
			DPU_DEBUG("cdp usage:%d rd:%d wr:%d\n",
				j, cfg->perf.cdp_cfg[j].rd_enable,
				cfg->perf.cdp_cfg[j].wr_enable);
		}

		cfg->has_cdp = true;
	}

freeprop:
	kfree(prop_value);
end:
	return rc;
}

static int dpu_hardware_format_caps(struct dpu_mdss_cfg *dpu_cfg,
	uint32_t hw_rev)
{
	int rc = 0;
	uint32_t dma_list_size, vig_list_size, wb2_list_size;
	uint32_t cursor_list_size = 0;
	uint32_t index = 0;

	if (IS_DPU_MAJOR_MINOR_SAME((hw_rev), DPU_HW_VER_300)) {
		cursor_list_size = ARRAY_SIZE(cursor_formats);
		dpu_cfg->cursor_formats = kcalloc(cursor_list_size,
			sizeof(struct dpu_format_extended), GFP_KERNEL);
		if (!dpu_cfg->cursor_formats) {
			rc = -ENOMEM;
			goto end;
		}
		index = dpu_copy_formats(dpu_cfg->cursor_formats,
			cursor_list_size, 0, cursor_formats,
			ARRAY_SIZE(cursor_formats));
	}

	dma_list_size = ARRAY_SIZE(plane_formats);
	vig_list_size = ARRAY_SIZE(plane_formats_yuv);
	wb2_list_size = ARRAY_SIZE(wb2_formats);

	dma_list_size += ARRAY_SIZE(rgb_10bit_formats);
	vig_list_size += ARRAY_SIZE(rgb_10bit_formats)
		+ ARRAY_SIZE(tp10_ubwc_formats)
		+ ARRAY_SIZE(p010_formats);
	if (IS_DPU_MAJOR_MINOR_SAME((hw_rev), DPU_HW_VER_400) ||
		(IS_DPU_MAJOR_MINOR_SAME((hw_rev), DPU_HW_VER_410)))
		vig_list_size += ARRAY_SIZE(p010_ubwc_formats);

	wb2_list_size += ARRAY_SIZE(rgb_10bit_formats)
		+ ARRAY_SIZE(tp10_ubwc_formats);

	dpu_cfg->dma_formats = kcalloc(dma_list_size,
		sizeof(struct dpu_format_extended), GFP_KERNEL);
	if (!dpu_cfg->dma_formats) {
		rc = -ENOMEM;
		goto end;
	}

	dpu_cfg->vig_formats = kcalloc(vig_list_size,
		sizeof(struct dpu_format_extended), GFP_KERNEL);
	if (!dpu_cfg->vig_formats) {
		rc = -ENOMEM;
		goto end;
	}

	dpu_cfg->wb_formats = kcalloc(wb2_list_size,
		sizeof(struct dpu_format_extended), GFP_KERNEL);
	if (!dpu_cfg->wb_formats) {
		DPU_ERROR("failed to allocate wb format list\n");
		rc = -ENOMEM;
		goto end;
	}

	index = dpu_copy_formats(dpu_cfg->dma_formats, dma_list_size,
		0, plane_formats, ARRAY_SIZE(plane_formats));
	index += dpu_copy_formats(dpu_cfg->dma_formats, dma_list_size,
		index, rgb_10bit_formats,
		ARRAY_SIZE(rgb_10bit_formats));

	index = dpu_copy_formats(dpu_cfg->vig_formats, vig_list_size,
		0, plane_formats_yuv, ARRAY_SIZE(plane_formats_yuv));
	index += dpu_copy_formats(dpu_cfg->vig_formats, vig_list_size,
		index, rgb_10bit_formats,
		ARRAY_SIZE(rgb_10bit_formats));
	index += dpu_copy_formats(dpu_cfg->vig_formats, vig_list_size,
		index, p010_formats, ARRAY_SIZE(p010_formats));
	if (IS_DPU_MAJOR_MINOR_SAME((hw_rev), DPU_HW_VER_400) ||
		(IS_DPU_MAJOR_MINOR_SAME((hw_rev), DPU_HW_VER_410)))
		index += dpu_copy_formats(dpu_cfg->vig_formats,
			vig_list_size, index, p010_ubwc_formats,
			ARRAY_SIZE(p010_ubwc_formats));

	index += dpu_copy_formats(dpu_cfg->vig_formats, vig_list_size,
		index, tp10_ubwc_formats,
		ARRAY_SIZE(tp10_ubwc_formats));

	index = dpu_copy_formats(dpu_cfg->wb_formats, wb2_list_size,
		0, wb2_formats, ARRAY_SIZE(wb2_formats));
	index += dpu_copy_formats(dpu_cfg->wb_formats, wb2_list_size,
		index, rgb_10bit_formats,
		ARRAY_SIZE(rgb_10bit_formats));
	index += dpu_copy_formats(dpu_cfg->wb_formats, wb2_list_size,
		index, tp10_ubwc_formats,
		ARRAY_SIZE(tp10_ubwc_formats));
end:
	return rc;
}

static int _dpu_hardware_caps(struct dpu_mdss_cfg *dpu_cfg, uint32_t hw_rev)
{
	int rc = 0;

	if (!dpu_cfg)
		return -EINVAL;

	rc = dpu_hardware_format_caps(dpu_cfg, hw_rev);

	if (IS_MSM8996_TARGET(hw_rev)) {
		/* update msm8996 target here */
		dpu_cfg->perf.min_prefill_lines = 21;
	} else if (IS_MSM8998_TARGET(hw_rev)) {
		/* update msm8998 target here */
		dpu_cfg->has_wb_ubwc = true;
		dpu_cfg->perf.min_prefill_lines = 25;
		dpu_cfg->vbif_qos_nlvl = 4;
		dpu_cfg->ts_prefill_rev = 1;
	} else if (IS_SDM845_TARGET(hw_rev) || IS_SDM670_TARGET(hw_rev)) {
		/* update sdm845 target here */
		dpu_cfg->has_wb_ubwc = true;
		dpu_cfg->perf.min_prefill_lines = 24;
		dpu_cfg->vbif_qos_nlvl = 8;
		dpu_cfg->ts_prefill_rev = 2;
	} else if (IS_SDM855_TARGET(hw_rev)) {
		dpu_cfg->has_wb_ubwc = true;
		dpu_cfg->perf.min_prefill_lines = 24;
	} else {
		DPU_ERROR("unsupported chipset id:%X\n", hw_rev);
		dpu_cfg->perf.min_prefill_lines = 0xffff;
		rc = -ENODEV;
	}

	return rc;
}

void dpu_hw_catalog_deinit(struct dpu_mdss_cfg *dpu_cfg)
{
	int i;

	if (!dpu_cfg)
		return;

	for (i = 0; i < dpu_cfg->sspp_count; i++)
		kfree(dpu_cfg->sspp[i].sblk);

	for (i = 0; i < dpu_cfg->mixer_count; i++)
		kfree(dpu_cfg->mixer[i].sblk);

	for (i = 0; i < dpu_cfg->wb_count; i++)
		kfree(dpu_cfg->wb[i].sblk);

	for (i = 0; i < dpu_cfg->dspp_count; i++)
		kfree(dpu_cfg->dspp[i].sblk);

	if (dpu_cfg->ds_count)
		kfree(dpu_cfg->ds[0].top);

	for (i = 0; i < dpu_cfg->pingpong_count; i++)
		kfree(dpu_cfg->pingpong[i].sblk);

	for (i = 0; i < dpu_cfg->vbif_count; i++) {
		kfree(dpu_cfg->vbif[i].dynamic_ot_rd_tbl.cfg);
		kfree(dpu_cfg->vbif[i].dynamic_ot_wr_tbl.cfg);
		kfree(dpu_cfg->vbif[i].qos_rt_tbl.priority_lvl);
		kfree(dpu_cfg->vbif[i].qos_nrt_tbl.priority_lvl);
	}

	for (i = 0; i < DPU_QOS_LUT_USAGE_MAX; i++)
		kfree(dpu_cfg->perf.qos_lut_tbl[i].entries);

	kfree(dpu_cfg->dma_formats);
	kfree(dpu_cfg->cursor_formats);
	kfree(dpu_cfg->vig_formats);
	kfree(dpu_cfg->wb_formats);

	kfree(dpu_cfg);
}

/*************************************************************
 * hardware catalog init
 *************************************************************/
struct dpu_mdss_cfg *dpu_hw_catalog_init(struct drm_device *dev, u32 hw_rev)
{
	int rc;
	struct dpu_mdss_cfg *dpu_cfg;
	struct device_node *np = dev->dev->of_node;

	dpu_cfg = kzalloc(sizeof(*dpu_cfg), GFP_KERNEL);
	if (!dpu_cfg)
		return ERR_PTR(-ENOMEM);

	dpu_cfg->hwversion = hw_rev;

	rc = _dpu_hardware_caps(dpu_cfg, hw_rev);
	if (rc)
		goto end;

	rc = dpu_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_perf_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_rot_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_ctl_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_sspp_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_dspp_top_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_dspp_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_ds_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_dsc_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_pp_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	/* mixer parsing should be done after dspp,
	 * ds and pp for mapping setup
	 */
	rc = dpu_mixer_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_intf_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_wb_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	/* cdm parsing should be done after intf and wb for mapping setup */
	rc = dpu_cdm_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_vbif_parse_dt(np, dpu_cfg);
	if (rc)
		goto end;

	rc = dpu_parse_reg_dma_dt(np, dpu_cfg);
	if (rc)
		goto end;

	return dpu_cfg;

end:
	dpu_hw_catalog_deinit(dpu_cfg);
	return NULL;
}
