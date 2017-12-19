// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Linaro Ltd
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/interconnect-provider.h>
#include <linux/interconnect/qcom.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "rpm.h"

extern struct qcom_interconnect_rpm interconnect_rpm;

#define RPM_MASTER_FIELD_BW	0x00007762
#define RPM_BUS_MASTER_REQ      0x73616d62
#define RPM_BUS_SLAVE_REQ       0x766c7362

struct qcom_interconnect_req {
	__le32 key;
	__le32 nbytes;
	__le32 value;
};

#define to_qcom_icp(_icp) \
	container_of(_icp, struct qcom_interconnect_provider, icp)
#define to_qcom_node(_node) \
	container_of(_node, struct qcom_interconnect_node, node)

#define DEFINE_QNODE(_name, _id, _port, _buswidth, _ap_owned,		\
			_mas_rpm_id, _slv_rpm_id, _qos_mode,		\
			_numlinks, ...)					\
		static struct qcom_interconnect_node _name = {		\
		.id = _id,						\
		.name = #_name,						\
		.port = _port,						\
		.buswidth = _buswidth,					\
		.qos_mode = _qos_mode,					\
		.ap_owned = _ap_owned,					\
		.mas_rpm_id = _mas_rpm_id,				\
		.slv_rpm_id = _slv_rpm_id,				\
		.num_links = _numlinks,					\
		.links = { __VA_ARGS__ },				\
	};

enum qcom_qos_mode {
	QCOM_QOS_MODE_BYPASS = 0,
	QCOM_QOS_MODE_FIXED,
	QCOM_QOS_MODE_MAX,
};

enum qcom_bus_type {
	QCOM_BUS_TYPE_NOC = 0,
	QCOM_BUS_TYPE_MEM,
};

struct qcom_interconnect_provider {
	struct icp		icp;
	void __iomem		*base;
	struct clk		*bus_clk;
	struct clk		*bus_a_clk;
	u32			base_offset;
	u32			qos_offset;
	enum qcom_bus_type	type;
};

#define MSM8916_MAX_LINKS	8

struct qcom_interconnect_node {
	struct interconnect_node node;
	unsigned char *name;
	struct interconnect_node *links[MSM8916_MAX_LINKS];
	u16 id;
	u16 num_links;
	u16 port;
	u16 buswidth; /* width of the interconnect between a node and the bus */
	bool ap_owned; /* the AP CPU does the writing to QoS registers */
	struct qcom_smd_rpm *rpm; /* reference to the RPM driver */
	enum qcom_qos_mode qos_mode; /* QoS mode to be programmed for this
				      * device, only applicable for AP owned
				      * resource.
				      */
	int mas_rpm_id;	/* mas_rpm_id:	For non-AP owned device this is the RPM
			 *  id for devices that are bus masters. This is the id
			 *  that is used when sending a message to RPM for this
			 *  device.
			 */
	int slv_rpm_id;	/* For non-AP owned device this is the RPM id for
			 * devices that are bus slaves. This is the id that is
			 * used when sending a message to RPM for this device.
			 */
	u64 rate; /* rate in Hz */
};

static struct qcom_interconnect_node mas_video;
static struct qcom_interconnect_node mas_jpeg;
static struct qcom_interconnect_node mas_vfe;
static struct qcom_interconnect_node mas_mdp;
static struct qcom_interconnect_node mas_qdss_bam;
static struct qcom_interconnect_node mas_snoc_cfg;
static struct qcom_interconnect_node mas_qdss_etr;
static struct qcom_interconnect_node mm_int_0;
static struct qcom_interconnect_node mm_int_1;
static struct qcom_interconnect_node mm_int_2;
static struct qcom_interconnect_node mm_int_bimc;
static struct qcom_interconnect_node snoc_int_0;
static struct qcom_interconnect_node snoc_int_1;
static struct qcom_interconnect_node snoc_int_bimc;
static struct qcom_interconnect_node snoc_bimc_0_mas;
static struct qcom_interconnect_node snoc_bimc_1_mas;
static struct qcom_interconnect_node qdss_int;
static struct qcom_interconnect_node bimc_snoc_slv;
static struct qcom_interconnect_node snoc_pnoc_mas;
static struct qcom_interconnect_node pnoc_snoc_slv;
static struct qcom_interconnect_node slv_srvc_snoc;
static struct qcom_interconnect_node slv_qdss_stm;
static struct qcom_interconnect_node slv_imem;
static struct qcom_interconnect_node slv_apss;
static struct qcom_interconnect_node slv_cats_0;
static struct qcom_interconnect_node slv_cats_1;

static struct qcom_interconnect_node mas_apss;
static struct qcom_interconnect_node mas_tcu0;
static struct qcom_interconnect_node mas_tcu1;
static struct qcom_interconnect_node mas_gfx;
static struct qcom_interconnect_node bimc_snoc_mas;
static struct qcom_interconnect_node snoc_bimc_0_slv;
static struct qcom_interconnect_node snoc_bimc_1_slv;
static struct qcom_interconnect_node slv_ebi_ch0;
static struct qcom_interconnect_node slv_apps_l2;

static struct qcom_interconnect_node snoc_pnoc_slv;
static struct qcom_interconnect_node pnoc_int_0;
static struct qcom_interconnect_node pnoc_int_1;
static struct qcom_interconnect_node pnoc_m_0;
static struct qcom_interconnect_node pnoc_m_1;
static struct qcom_interconnect_node pnoc_s_0;
static struct qcom_interconnect_node pnoc_s_1;
static struct qcom_interconnect_node pnoc_s_2;
static struct qcom_interconnect_node pnoc_s_3;
static struct qcom_interconnect_node pnoc_s_4;
static struct qcom_interconnect_node pnoc_s_8;
static struct qcom_interconnect_node pnoc_s_9;
static struct qcom_interconnect_node slv_imem_cfg;
static struct qcom_interconnect_node slv_crypto_0_cfg;
static struct qcom_interconnect_node slv_msg_ram;
static struct qcom_interconnect_node slv_pdm;
static struct qcom_interconnect_node slv_prng;
static struct qcom_interconnect_node slv_clk_ctl;
static struct qcom_interconnect_node slv_mss;
static struct qcom_interconnect_node slv_tlmm;
static struct qcom_interconnect_node slv_tcsr;
static struct qcom_interconnect_node slv_security;
static struct qcom_interconnect_node slv_spdm;
static struct qcom_interconnect_node slv_pnoc_cfg;
static struct qcom_interconnect_node slv_pmic_arb;
static struct qcom_interconnect_node slv_bimc_cfg;
static struct qcom_interconnect_node slv_boot_rom;
static struct qcom_interconnect_node slv_mpm;
static struct qcom_interconnect_node slv_qdss_cfg;
static struct qcom_interconnect_node slv_rbcpr_cfg;
static struct qcom_interconnect_node slv_snoc_cfg;
static struct qcom_interconnect_node slv_dehr_cfg;
static struct qcom_interconnect_node slv_venus_cfg;
static struct qcom_interconnect_node slv_display_cfg;
static struct qcom_interconnect_node slv_camera_cfg;
static struct qcom_interconnect_node slv_usb_hs;
static struct qcom_interconnect_node slv_sdcc_1;
static struct qcom_interconnect_node slv_blsp_1;
static struct qcom_interconnect_node slv_sdcc_2;
static struct qcom_interconnect_node slv_gfx_cfg;
static struct qcom_interconnect_node slv_audio;
static struct qcom_interconnect_node mas_blsp_1;
static struct qcom_interconnect_node mas_spdm;
static struct qcom_interconnect_node mas_dehr;
static struct qcom_interconnect_node mas_audio;
static struct qcom_interconnect_node mas_usb_hs;
static struct qcom_interconnect_node mas_pnoc_crypto_0;
static struct qcom_interconnect_node mas_pnoc_sdcc_1;
static struct qcom_interconnect_node mas_pnoc_sdcc_2;
static struct qcom_interconnect_node pnoc_snoc_mas;

struct qcom_interconnect_desc {
	struct qcom_interconnect_node **nodes;
	size_t num_nodes;
};

DEFINE_QNODE(mas_video, 63, 8, 16, 1, 0, 0, QCOM_QOS_MODE_BYPASS, 2, &mm_int_0.node, &mm_int_2.node);
DEFINE_QNODE(mas_jpeg, 62, 6, 16, 1, 0, 0, QCOM_QOS_MODE_BYPASS, 2, &mm_int_0.node, &mm_int_2.node);
DEFINE_QNODE(mas_vfe, 29, 9, 16, 1, 0, 0, QCOM_QOS_MODE_BYPASS, 2, &mm_int_1.node, &mm_int_2.node);
DEFINE_QNODE(mas_mdp, 22, 7, 16, 1, 0, 0, QCOM_QOS_MODE_BYPASS, 2, &mm_int_0.node, &mm_int_2.node);
DEFINE_QNODE(mas_qdss_bam, 53, 11, 16, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &qdss_int.node);
DEFINE_QNODE(mas_snoc_cfg, 54, 11, 16, 0, 20, 0, QCOM_QOS_MODE_BYPASS, 1, &qdss_int.node);
DEFINE_QNODE(mas_qdss_etr, 60, 10, 16, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &qdss_int.node);
DEFINE_QNODE(mm_int_0, 10000, 10, 16, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &mm_int_bimc.node);
DEFINE_QNODE(mm_int_1, 10001, 10, 16, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &mm_int_bimc.node);
DEFINE_QNODE(mm_int_2, 10002, 10, 16, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &snoc_int_0.node);
DEFINE_QNODE(mm_int_bimc, 10003, 10, 16, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &snoc_bimc_1_mas.node);
DEFINE_QNODE(snoc_int_0, 10004, 10, 8, 0, 99, 130, QCOM_QOS_MODE_FIXED, 3, &slv_qdss_stm.node, &slv_imem.node, &snoc_pnoc_mas.node);
DEFINE_QNODE(snoc_int_1, 10005, 10, 8, 0, 100, 131, QCOM_QOS_MODE_FIXED, 3, &slv_apss.node, &slv_cats_0.node, &slv_cats_1.node);
DEFINE_QNODE(snoc_int_bimc, 10006, 10, 8, 0, 101, 132, QCOM_QOS_MODE_FIXED, 1, &snoc_bimc_0_mas.node);
DEFINE_QNODE(snoc_bimc_0_mas, 10007, 10, 8, 0, 3, 0, QCOM_QOS_MODE_FIXED, 1, &snoc_bimc_0_slv.node);
DEFINE_QNODE(snoc_bimc_1_mas, 10008, 10, 16, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &snoc_bimc_1_slv.node);
DEFINE_QNODE(qdss_int, 10009, 10, 8, 1, 0, 0, QCOM_QOS_MODE_FIXED, 2, &snoc_int_0.node, &snoc_int_bimc.node);
DEFINE_QNODE(bimc_snoc_slv, 10017, 10, 8, 1, 0, 0, QCOM_QOS_MODE_FIXED, 2, &snoc_int_0.node, &snoc_int_1.node);
DEFINE_QNODE(snoc_pnoc_mas, 10027, 10, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &snoc_pnoc_slv.node);
DEFINE_QNODE(pnoc_snoc_slv, 10011, 10, 8, 0, 0, 45, QCOM_QOS_MODE_FIXED, 3, &snoc_int_0.node, &snoc_int_bimc.node, &snoc_int_1.node);
DEFINE_QNODE(slv_srvc_snoc, 587, 10, 8, 0, 0, 29, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_qdss_stm, 588, 10, 4, 0, 0, 30, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_imem, 519, 10, 8, 0, 0, 26, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_apss, 517, 10, 4, 0, 0, 20, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_cats_0, 663, 10, 16, 0, 0, 106, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_cats_1, 664, 10, 8, 0, 0, 107, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(mas_apss, 1, 0, 8, 1, 0, 0, QCOM_QOS_MODE_FIXED, 3, &slv_ebi_ch0.node, &bimc_snoc_mas.node, &slv_apps_l2.node);
DEFINE_QNODE(mas_tcu0, 104, 5, 8, 1, 0, 0, QCOM_QOS_MODE_FIXED, 3, &slv_ebi_ch0.node, &bimc_snoc_mas.node, &slv_apps_l2.node);
DEFINE_QNODE(mas_tcu1, 105, 6, 8, 1, 0, 0, QCOM_QOS_MODE_FIXED, 3, &slv_ebi_ch0.node, &bimc_snoc_mas.node, &slv_apps_l2.node);
DEFINE_QNODE(mas_gfx, 26, 2, 8, 1, 0, 0, QCOM_QOS_MODE_FIXED, 3, &slv_ebi_ch0.node, &bimc_snoc_mas.node, &slv_apps_l2.node);
DEFINE_QNODE(bimc_snoc_mas, 10016, 2, 8, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &bimc_snoc_slv.node);
DEFINE_QNODE(snoc_bimc_0_slv, 10025, 2, 8, 0, 0, 24, QCOM_QOS_MODE_FIXED, 1, &slv_ebi_ch0.node);
DEFINE_QNODE(snoc_bimc_1_slv, 10026, 2, 8, 1, 0, 0, QCOM_QOS_MODE_FIXED, 1, &slv_ebi_ch0.node);
DEFINE_QNODE(slv_ebi_ch0, 512, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_apps_l2, 514, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(snoc_pnoc_slv, 10028, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_int_0.node);
DEFINE_QNODE(pnoc_int_0, 10012, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 8, &pnoc_snoc_mas.node, &pnoc_s_0.node, &pnoc_s_1.node, &pnoc_s_2.node, &pnoc_s_3.node, &pnoc_s_4.node, &pnoc_s_8.node, &pnoc_s_9.node);
DEFINE_QNODE(pnoc_int_1, 10013, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_snoc_mas.node);
DEFINE_QNODE(pnoc_m_0, 10014, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_int_0.node);
DEFINE_QNODE(pnoc_m_1, 10015, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_snoc_mas.node);
DEFINE_QNODE(pnoc_s_0, 10018, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 5, &slv_clk_ctl.node, &slv_tlmm.node, &slv_tcsr.node, &slv_security.node, &slv_mss.node);
DEFINE_QNODE(pnoc_s_1, 10019, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 5, &slv_imem_cfg.node, &slv_crypto_0_cfg.node, &slv_msg_ram.node, &slv_pdm.node, &slv_prng.node);
DEFINE_QNODE(pnoc_s_2, 10020, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 5, &slv_spdm.node, &slv_boot_rom.node, &slv_bimc_cfg.node, &slv_pnoc_cfg.node, &slv_pmic_arb.node);
DEFINE_QNODE(pnoc_s_3, 10021, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 5, &slv_mpm.node, &slv_snoc_cfg.node, &slv_rbcpr_cfg.node, &slv_qdss_cfg.node, &slv_dehr_cfg.node);
DEFINE_QNODE(pnoc_s_4, 10022, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 3, &slv_venus_cfg.node, &slv_camera_cfg.node, &slv_display_cfg.node);
DEFINE_QNODE(pnoc_s_8, 10023, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 3, &slv_usb_hs.node, &slv_sdcc_1.node, &slv_blsp_1.node);
DEFINE_QNODE(pnoc_s_9, 10024, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 3, &slv_sdcc_2.node, &slv_audio.node, &slv_gfx_cfg.node);
DEFINE_QNODE(slv_imem_cfg, 627, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_crypto_0_cfg, 625, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_msg_ram, 535, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pdm, 577, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_prng, 618, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_clk_ctl, 620, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_mss, 521, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_tlmm, 624, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_tcsr, 579, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_security, 622, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_spdm, 533, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pnoc_cfg, 641, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pmic_arb, 632, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_bimc_cfg, 629, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_boot_rom, 630, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_mpm, 536, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_qdss_cfg, 635, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_rbcpr_cfg, 636, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_snoc_cfg, 647, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_dehr_cfg, 634, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_venus_cfg, 596, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_display_cfg, 590, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_camera_cfg, 589, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_usb_hs, 614, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_sdcc_1, 606, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_blsp_1, 613, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_sdcc_2, 609, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_gfx_cfg, 598, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_audio, 522, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(mas_blsp_1, 86, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_m_1.node);
DEFINE_QNODE(mas_spdm, 36, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_m_0.node);
DEFINE_QNODE(mas_dehr, 75, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_m_0.node);
DEFINE_QNODE(mas_audio, 15, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_m_0.node);
DEFINE_QNODE(mas_usb_hs, 87, 2, 4, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_m_1.node);
DEFINE_QNODE(mas_pnoc_crypto_0, 55, 2, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_int_1.node);
DEFINE_QNODE(mas_pnoc_sdcc_1, 78, 7, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_int_1.node);
DEFINE_QNODE(mas_pnoc_sdcc_2, 81, 8, 8, 0, 0, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_int_1.node);
DEFINE_QNODE(pnoc_snoc_mas, 10010, 8, 8, 0, 29, 0, QCOM_QOS_MODE_FIXED, 1, &pnoc_snoc_slv.node);

static struct qcom_interconnect_node *msm8916_snoc_nodes[] = {
	&mas_video,
	&mas_jpeg,
	&mas_vfe,
	&mas_mdp,
	&mas_qdss_bam,
	&mas_snoc_cfg,
	&mas_qdss_etr,
	&mm_int_0,
	&mm_int_1,
	&mm_int_2,
	&mm_int_bimc,
	&snoc_int_0,
	&snoc_int_1,
	&snoc_int_bimc,
	&snoc_bimc_0_mas,
	&snoc_bimc_1_mas,
	&qdss_int,
	&bimc_snoc_slv,
	&snoc_pnoc_mas,
	&pnoc_snoc_slv,
	&slv_srvc_snoc,
	&slv_qdss_stm,
	&slv_imem,
	&slv_apss,
	&slv_cats_0,
	&slv_cats_1,
};

static struct qcom_interconnect_desc msm8916_snoc = {
	.nodes = msm8916_snoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8916_snoc_nodes),
};

static struct qcom_interconnect_node *msm8916_bimc_nodes[] = {
	&mas_apss,
	&mas_tcu0,
	&mas_tcu1,
	&mas_gfx,
	&bimc_snoc_mas,
	&snoc_bimc_0_slv,
	&snoc_bimc_1_slv,
	&slv_ebi_ch0,
	&slv_apps_l2,
};

static struct qcom_interconnect_desc msm8916_bimc = {
	.nodes = msm8916_bimc_nodes,
	.num_nodes = ARRAY_SIZE(msm8916_bimc_nodes),
};

static struct qcom_interconnect_node *msm8916_pnoc_nodes[] = {
	&snoc_pnoc_slv,
	&pnoc_int_0,
	&pnoc_int_1,
	&pnoc_m_0,
	&pnoc_m_1,
	&pnoc_s_0,
	&pnoc_s_1,
	&pnoc_s_2,
	&pnoc_s_3,
	&pnoc_s_4,
	&pnoc_s_8,
	&pnoc_s_9,
	&slv_imem_cfg,
	&slv_crypto_0_cfg,
	&slv_msg_ram,
	&slv_pdm,
	&slv_prng,
	&slv_clk_ctl,
	&slv_mss,
	&slv_tlmm,
	&slv_tcsr,
	&slv_security,
	&slv_spdm,
	&slv_pnoc_cfg,
	&slv_pmic_arb,
	&slv_bimc_cfg,
	&slv_boot_rom,
	&slv_mpm,
	&slv_qdss_cfg,
	&slv_rbcpr_cfg,
	&slv_snoc_cfg,
	&slv_dehr_cfg,
	&slv_venus_cfg,
	&slv_display_cfg,
	&slv_camera_cfg,
	&slv_usb_hs,
	&slv_sdcc_1,
	&slv_blsp_1,
	&slv_sdcc_2,
	&slv_gfx_cfg,
	&slv_audio,
	&mas_blsp_1,
	&mas_spdm,
	&mas_dehr,
	&mas_audio,
	&mas_usb_hs,
	&mas_pnoc_crypto_0,
	&mas_pnoc_sdcc_1,
	&mas_pnoc_sdcc_2,
	&pnoc_snoc_mas,
};

static struct qcom_interconnect_desc msm8916_pnoc = {
	.nodes = msm8916_pnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8916_pnoc_nodes),
};

static int qcom_interconnect_init(struct interconnect_node *node)
{
	struct qcom_interconnect_node *qn = to_qcom_node(node);
	struct qcom_interconnect_provider *qicp = to_qcom_icp(node->icp);
	int ret;

	/* populate default values */
	if (!qn->buswidth)
		qn->buswidth = 8;

	/* TODO: init qos and priority */

	ret = clk_prepare_enable(qicp->bus_clk);
	if (ret)
		pr_info("%s: error enabling bus clk (%d)\n", __func__, ret);
	ret = clk_prepare_enable(qicp->bus_a_clk);
	if (ret)
		pr_info("%s: error enabling bus_a clk (%d)\n", __func__, ret);

	return 0;
}

static int qcom_interconnect_set(struct interconnect_node *src,
				 struct interconnect_node *dst,
				 struct interconnect_creq *creq)
{
	struct qcom_interconnect_provider *qicp;
	struct qcom_interconnect_node *qn;
	struct interconnect_node *node;
	struct icp *icp;
	u64 avg_bw = 0;
	u64 peak_bw = 0;
	u64 rate = 0;
	int ret = 0;

	if (!src && !dst)
		return -ENODEV;

	if (!src)
		node = dst;
	else
		node = src;

	qn = to_qcom_node(node);
	icp = qn->node.icp;
	qicp = to_qcom_icp(node->icp);

	avg_bw = icp->creq.avg_bw;
	peak_bw = icp->creq.peak_bw;

	/* convert from kbps to bps */
	avg_bw *= 1000ULL;
	peak_bw *= 1000ULL;

	/* set bandwidth */
	if (qn->ap_owned) {
		/* TODO: set QoS */
	} else {
		/* send message to the RPM processor */

		if (qn->mas_rpm_id != -1) {
			ret = qcom_interconnect_rpm_send(QCOM_SMD_RPM_ACTIVE_STATE,
							 RPM_BUS_MASTER_REQ,
							 qn->mas_rpm_id,
							 avg_bw);
		}

		if (qn->slv_rpm_id != -1) {
			ret = qcom_interconnect_rpm_send(QCOM_SMD_RPM_ACTIVE_STATE,
							 RPM_BUS_SLAVE_REQ,
							 qn->slv_rpm_id,
							 avg_bw);
		}
	}

	rate = max(avg_bw, peak_bw);

	do_div(rate, qn->buswidth);

	if (qn->rate != rate) {
		ret = clk_set_rate(qicp->bus_clk, rate);
		if (ret) {
			pr_err("set clk rate %lld error %d\n", rate, ret);
			return ret;
		}

		ret = clk_set_rate(qicp->bus_a_clk, rate);
		if (ret) {
			pr_err("set clk rate %lld error %d\n", rate, ret);
			return ret;
		}

		qn->rate = rate;
	}

	return ret;
}

struct interconnect_onecell_data {
	struct interconnect_node **nodes;
	unsigned int num_nodes;
};

static const struct icp_ops qcom_ops = {
	.set = qcom_interconnect_set,
};

static int qnoc_probe(struct platform_device *pdev)
{
	struct qcom_interconnect_provider *qicp;
	struct icp *icp;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;
	void __iomem *base;
	struct clk *bus_clk, *bus_a_clk;
	size_t num_nodes, i;
	const struct qcom_interconnect_desc *desc;
	struct qcom_interconnect_node **qnodes;
	u32 type, base_offset, qos_offset;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	/* wait for RPM */
	if (!interconnect_rpm.rpm)
		return -EPROBE_DEFER;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qicp = devm_kzalloc(dev, sizeof(*qicp), GFP_KERNEL);
	if (!qicp)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	bus_clk = devm_clk_get(&pdev->dev, "bus_clk");
	if (IS_ERR(bus_clk))
		return PTR_ERR(bus_clk);
	bus_a_clk = devm_clk_get(&pdev->dev, "bus_a_clk");
	if (IS_ERR(bus_a_clk))
		return PTR_ERR(bus_a_clk);

	of_property_read_u32(np, "type", &type);
	of_property_read_u32(np, "base-offset", &base_offset);
	of_property_read_u32(np, "qos-offset", &qos_offset);

	qicp->base = base;
	qicp->type = type;
	qicp->base_offset = base_offset;
	qicp->qos_offset = qos_offset;
	qicp->bus_clk = bus_clk;
	qicp->bus_a_clk = bus_a_clk;
	icp = &qicp->icp;
	icp->dev = dev;
	icp->ops = &qcom_ops;
	INIT_LIST_HEAD(&icp->nodes);

	for (i = 0; i < num_nodes; i++) {
		struct interconnect_node *node;
		int ret;
		size_t j;

		if (!qnodes[i])
			continue;

		node = &qnodes[i]->node;
		node->id = qnodes[i]->id;
		node->icp = icp;
		node->num_links = qnodes[i]->num_links;
		node->links = devm_kcalloc(dev, node->num_links,
					   sizeof(*node->links), GFP_KERNEL);
		if (!node->links)
			return -ENOMEM;

		/* populate links */
		for (j = 0; j < node->num_links; j++)
			node->links[j] = qnodes[i]->links[j];

		/* add the node to interconnect provider */
		list_add_tail(&node->icn_list, &icp->nodes);
		dev_dbg(&pdev->dev, "registered node %p %s %d\n", node,
			qnodes[i]->name, node->id);

		ret = qcom_interconnect_init(node);
		if (ret)
			dev_err(&pdev->dev, "node init error (%d)\n", ret);
	}

	return interconnect_add_provider(icp);
}

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,msm8916-pnoc", .data = &msm8916_pnoc },
	{ .compatible = "qcom,msm8916-snoc", .data = &msm8916_snoc },
	{ .compatible = "qcom,msm8916-bimc", .data = &msm8916_bimc },
	{ },
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.driver = {
		.name = "qnoc-msm8916",
		.of_match_table = qnoc_of_match,
	},
};
module_platform_driver(qnoc_driver);
MODULE_AUTHOR("Georgi Djakov <georgi.djakov@linaro.org>");
MODULE_DESCRIPTION("Qualcomm msm8916 NoC driver");
MODULE_LICENSE("GPL v2");
