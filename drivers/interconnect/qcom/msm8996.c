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

#define DEFINE_QNODE(_name, _id, _port, _agg_ports, _buswidth,		\
			_ap_owned, _mas_rpm_id, _slv_rpm_id, _qos_mode, \
			_numlinks, ...)					\
		static struct qcom_interconnect_node _name = {		\
		.id = _id,						\
		.name = #_name,						\
		.port = _port,						\
		.agg_ports = _agg_ports,				\
		.buswidth = _buswidth,					\
		.qos_mode = _qos_mode,					\
		.ap_owned = _ap_owned,					\
		.mas_rpm_id = _mas_rpm_id,				\
		.slv_rpm_id = _slv_rpm_id,				\
		.num_links = _numlinks,					\
		.links = { __VA_ARGS__ },				\
	};

struct qcom_interconnect_desc {
	struct qcom_interconnect_node **nodes;
	size_t num_nodes;
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

#define MSM8996_MAX_LINKS       38

struct qcom_interconnect_node {
	struct interconnect_node node;
	unsigned char *name;
	struct interconnect_node *links[MSM8996_MAX_LINKS];
	u16 id;
	u16 num_links;
	u16 port;
	u16 agg_ports; /* The number of aggregation ports on the bus */
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
	u64 rate;
};

static struct qcom_interconnect_node mas_pcie_0;
static struct qcom_interconnect_node mas_pcie_1;
static struct qcom_interconnect_node mas_pcie_2;
static struct qcom_interconnect_node mas_cnoc_a1noc;
static struct qcom_interconnect_node mas_crypto_c0;
static struct qcom_interconnect_node mas_pnoc_a1noc;
static struct qcom_interconnect_node mas_usb3;
static struct qcom_interconnect_node mas_ipa;
static struct qcom_interconnect_node mas_ufs;
static struct qcom_interconnect_node mas_apps_proc;
static struct qcom_interconnect_node mas_oxili;
static struct qcom_interconnect_node mas_mnoc_bimc;
static struct qcom_interconnect_node mas_snoc_bimc;
static struct qcom_interconnect_node mas_snoc_cnoc;
static struct qcom_interconnect_node mas_qdss_dap;
static struct qcom_interconnect_node mas_cnoc_mnoc_mmss_cfg;
static struct qcom_interconnect_node mas_cnoc_mnoc_cfg;
static struct qcom_interconnect_node mas_cpp;
static struct qcom_interconnect_node mas_jpeg;
static struct qcom_interconnect_node mas_mdp_p0;
static struct qcom_interconnect_node mas_mdp_p1;
static struct qcom_interconnect_node mas_rotator;
static struct qcom_interconnect_node mas_venus;
static struct qcom_interconnect_node mas_vfe;
static struct qcom_interconnect_node mas_snoc_vmem;
static struct qcom_interconnect_node mas_venus_vmem;
static struct qcom_interconnect_node mas_snoc_pnoc;
static struct qcom_interconnect_node mas_sdcc_1;
static struct qcom_interconnect_node mas_sdcc_2;
static struct qcom_interconnect_node mas_sdcc_4;
static struct qcom_interconnect_node mas_usb_hs;
static struct qcom_interconnect_node mas_blsp_1;
static struct qcom_interconnect_node mas_blsp_2;
static struct qcom_interconnect_node mas_tsif;
static struct qcom_interconnect_node mas_hmss;
static struct qcom_interconnect_node mas_qdss_bam;
static struct qcom_interconnect_node mas_snoc_cfg;
static struct qcom_interconnect_node mas_bimc_snoc_0;
static struct qcom_interconnect_node mas_bimc_snoc_1;
static struct qcom_interconnect_node mas_a0noc_snoc;
static struct qcom_interconnect_node mas_a1noc_snoc;
static struct qcom_interconnect_node mas_a2noc_snoc;
static struct qcom_interconnect_node mas_qdss_etr;
static struct qcom_interconnect_node slv_a0noc_snoc;
static struct qcom_interconnect_node slv_a1noc_snoc;
static struct qcom_interconnect_node slv_a2noc_snoc;
static struct qcom_interconnect_node slv_ebi;
static struct qcom_interconnect_node slv_hmss_l3;
static struct qcom_interconnect_node slv_bimc_snoc_0;
static struct qcom_interconnect_node slv_bimc_snoc_1;
static struct qcom_interconnect_node slv_cnoc_a1noc;
static struct qcom_interconnect_node slv_clk_ctl;
static struct qcom_interconnect_node slv_tcsr;
static struct qcom_interconnect_node slv_tlmm;
static struct qcom_interconnect_node slv_crypto0_cfg;
static struct qcom_interconnect_node slv_mpm;
static struct qcom_interconnect_node slv_pimem_cfg;
static struct qcom_interconnect_node slv_imem_cfg;
static struct qcom_interconnect_node slv_message_ram;
static struct qcom_interconnect_node slv_bimc_cfg;
static struct qcom_interconnect_node slv_pmic_arb;
static struct qcom_interconnect_node slv_prng;
static struct qcom_interconnect_node slv_dcc_cfg;
static struct qcom_interconnect_node slv_rbcpr_mx;
static struct qcom_interconnect_node slv_qdss_cfg;
static struct qcom_interconnect_node slv_rbcpr_cx;
static struct qcom_interconnect_node slv_cpr_apu_cfg;
static struct qcom_interconnect_node slv_cnoc_mnoc_cfg;
static struct qcom_interconnect_node slv_snoc_cfg;
static struct qcom_interconnect_node slv_snoc_mpu_cfg;
static struct qcom_interconnect_node slv_ebi1_phy_cfg;
static struct qcom_interconnect_node slv_a0noc_cfg;
static struct qcom_interconnect_node slv_pcie_1_cfg;
static struct qcom_interconnect_node slv_pcie_2_cfg;
static struct qcom_interconnect_node slv_pcie_0_cfg;
static struct qcom_interconnect_node slv_pcie20_ahb2phy;
static struct qcom_interconnect_node slv_a0noc_mpu_cfg;
static struct qcom_interconnect_node slv_ufs_cfg;
static struct qcom_interconnect_node slv_a1noc_cfg;
static struct qcom_interconnect_node slv_a1noc_mpu_cfg;
static struct qcom_interconnect_node slv_a2noc_cfg;
static struct qcom_interconnect_node slv_a2noc_mpu_cfg;
static struct qcom_interconnect_node slv_ssc_cfg;
static struct qcom_interconnect_node slv_a0noc_smmu_cfg;
static struct qcom_interconnect_node slv_a1noc_smmu_cfg;
static struct qcom_interconnect_node slv_a2noc_smmu_cfg;
static struct qcom_interconnect_node slv_lpass_smmu_cfg;
static struct qcom_interconnect_node slv_cnoc_mnoc_mmss_cfg;
static struct qcom_interconnect_node slv_mmagic_cfg;
static struct qcom_interconnect_node slv_cpr_cfg;
static struct qcom_interconnect_node slv_misc_cfg;
static struct qcom_interconnect_node slv_venus_throttle_cfg;
static struct qcom_interconnect_node slv_venus_cfg;
static struct qcom_interconnect_node slv_vmem_cfg;
static struct qcom_interconnect_node slv_dsa_cfg;
static struct qcom_interconnect_node slv_mnoc_clocks_cfg;
static struct qcom_interconnect_node slv_dsa_mpu_cfg;
static struct qcom_interconnect_node slv_mnoc_mpu_cfg;
static struct qcom_interconnect_node slv_display_cfg;
static struct qcom_interconnect_node slv_display_throttle_cfg;
static struct qcom_interconnect_node slv_camera_cfg;
static struct qcom_interconnect_node slv_camera_throttle_cfg;
static struct qcom_interconnect_node slv_oxili_cfg;
static struct qcom_interconnect_node slv_smmu_mdp_cfg;
static struct qcom_interconnect_node slv_smmu_rot_cfg;
static struct qcom_interconnect_node slv_smmu_venus_cfg;
static struct qcom_interconnect_node slv_smmu_cpp_cfg;
static struct qcom_interconnect_node slv_smmu_jpeg_cfg;
static struct qcom_interconnect_node slv_smmu_vfe_cfg;
static struct qcom_interconnect_node slv_mnoc_bimc;
static struct qcom_interconnect_node slv_vmem;
static struct qcom_interconnect_node slv_srvc_mnoc;
static struct qcom_interconnect_node slv_pnoc_a1noc;
static struct qcom_interconnect_node slv_usb_hs;
static struct qcom_interconnect_node slv_sdcc_2;
static struct qcom_interconnect_node slv_sdcc_4;
static struct qcom_interconnect_node slv_tsif;
static struct qcom_interconnect_node slv_blsp_2;
static struct qcom_interconnect_node slv_sdcc_1;
static struct qcom_interconnect_node slv_blsp_1;
static struct qcom_interconnect_node slv_pdm;
static struct qcom_interconnect_node slv_ahb2phy;
static struct qcom_interconnect_node slv_hmss;
static struct qcom_interconnect_node slv_lpass;
static struct qcom_interconnect_node slv_usb3;
static struct qcom_interconnect_node slv_snoc_bimc;
static struct qcom_interconnect_node slv_snoc_cnoc;
static struct qcom_interconnect_node slv_imem;
static struct qcom_interconnect_node slv_pimem;
static struct qcom_interconnect_node slv_snoc_vmem;
static struct qcom_interconnect_node slv_snoc_pnoc;
static struct qcom_interconnect_node slv_qdss_stm;
static struct qcom_interconnect_node slv_pcie_0;
static struct qcom_interconnect_node slv_pcie_1;
static struct qcom_interconnect_node slv_pcie_2;
static struct qcom_interconnect_node slv_srvc_snoc;

DEFINE_QNODE(mas_pcie_0, MASTER_PCIE, 0, 1, 8, 1, ICBID_MASTER_PCIE_0, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a0noc_snoc.node);
DEFINE_QNODE(mas_pcie_1, MASTER_PCIE_1, 1, 1, 8, 1, ICBID_MASTER_PCIE_1, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a0noc_snoc.node);
DEFINE_QNODE(mas_pcie_2, MASTER_PCIE_2, 2, 1, 8, 1, ICBID_MASTER_PCIE_2, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a0noc_snoc.node);
DEFINE_QNODE(mas_cnoc_a1noc, CNOC_A1NOC_MAS, 2, 1, 8, 1, ICBID_MASTER_CNOC_A1NOC, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a1noc_snoc.node);
DEFINE_QNODE(mas_crypto_c0, MASTER_CRYPTO_CORE0, 0, 1, 8, 1, ICBID_MASTER_CRYPTO_CORE0, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a1noc_snoc.node);
DEFINE_QNODE(mas_pnoc_a1noc, PNOC_A1NOC_MAS, 1, 1, 8, 0, ICBID_MASTER_PNOC_A1NOC, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a1noc_snoc.node);
DEFINE_QNODE(mas_usb3, MASTER_USB3, 3, 1, 8, 1, ICBID_MASTER_USB3_0, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a2noc_snoc.node);
DEFINE_QNODE(mas_ipa, MASTER_IPA, 3, 1, 8, 1, ICBID_MASTER_IPA, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a2noc_snoc.node);
DEFINE_QNODE(mas_ufs, MASTER_UFS, 2, 1, 8, 1, ICBID_MASTER_UFS, 0, QCOM_QOS_MODE_FIXED, 1, &slv_a2noc_snoc.node);
DEFINE_QNODE(mas_apps_proc, MASTER_AMPSS_M0, 0, 2, 8, 1, ICBID_MASTER_APPSS_PROC, 0, QCOM_QOS_MODE_FIXED, 3, &slv_bimc_snoc_1.node, &slv_ebi.node, &slv_bimc_snoc_0.node);
DEFINE_QNODE(mas_oxili, MASTER_GRAPHICS_3D, 1, 2, 8, 1, ICBID_MASTER_GFX3D, 0, QCOM_QOS_MODE_BYPASS, 0, 0);
DEFINE_QNODE(mas_mnoc_bimc, MNOC_BIMC_MAS, 2, 2, 8, 1, ICBID_MASTER_MNOC_BIMC, 0, QCOM_QOS_MODE_BYPASS, 4, &slv_bimc_snoc_1.node, &slv_hmss_l3.node, &slv_ebi.node, &slv_bimc_snoc_0.node);
DEFINE_QNODE(mas_snoc_bimc, SNOC_BIMC_MAS, 2, 2, 8, 0, ICBID_MASTER_SNOC_BIMC, 0, QCOM_QOS_MODE_BYPASS, 2, &slv_hmss_l3.node, &slv_ebi.node);
DEFINE_QNODE(mas_snoc_cnoc, SNOC_CNOC_MAS, 2, 1, 8, 0, ICBID_MASTER_SNOC_CNOC, 0, QCOM_QOS_MODE_BYPASS, 0, 0);
DEFINE_QNODE(mas_qdss_dap, MASTER_QDSS_DAP, 2, 1, 8, 1, ICBID_MASTER_QDSS_DAP, 0, QCOM_QOS_MODE_BYPASS, 38, &slv_cpr_apu_cfg.node, &slv_rbcpr_cx.node, &slv_a2noc_smmu_cfg.node, &slv_a0noc_mpu_cfg.node, &slv_message_ram.node, &slv_pcie_0_cfg.node, &slv_tlmm.node, &slv_mpm.node, &slv_a0noc_smmu_cfg.node, &slv_ebi1_phy_cfg.node, &slv_bimc_cfg.node, &slv_pimem_cfg.node, &slv_rbcpr_mx.node, &slv_clk_ctl.node, &slv_prng.node, &slv_pcie20_ahb2phy.node, &slv_a2noc_mpu_cfg.node, &slv_qdss_cfg.node, &slv_a2noc_cfg.node, &slv_a0noc_cfg.node, &slv_ufs_cfg.node, &slv_crypto0_cfg.node, &slv_cnoc_a1noc.node, &slv_pcie_1_cfg.node, &slv_snoc_cfg.node, &slv_snoc_mpu_cfg.node, &slv_a1noc_mpu_cfg.node, &slv_a1noc_smmu_cfg.node, &slv_pcie_2_cfg.node, &slv_cnoc_mnoc_cfg.node, &slv_cnoc_mnoc_mmss_cfg.node, &slv_pmic_arb.node, &slv_imem_cfg.node, &slv_a1noc_cfg.node, &slv_ssc_cfg.node, &slv_tcsr.node, &slv_lpass_smmu_cfg.node, &slv_dcc_cfg.node);
DEFINE_QNODE(mas_cnoc_mnoc_mmss_cfg, MASTER_CNOC_MNOC_MMSS_CFG, 2, 1, 8, 1, ICBID_MASTER_CNOC_MNOC_MMSS_CFG, 0, QCOM_QOS_MODE_BYPASS, 21, &slv_mmagic_cfg.node, &slv_dsa_mpu_cfg.node, &slv_mnoc_clocks_cfg.node, &slv_camera_throttle_cfg.node, &slv_venus_cfg.node, &slv_smmu_vfe_cfg.node, &slv_misc_cfg.node, &slv_smmu_cpp_cfg.node, &slv_oxili_cfg.node, &slv_display_throttle_cfg.node, &slv_venus_throttle_cfg.node, &slv_camera_cfg.node, &slv_display_cfg.node, &slv_cpr_cfg.node, &slv_smmu_rot_cfg.node, &slv_dsa_cfg.node, &slv_smmu_venus_cfg.node, &slv_vmem_cfg.node, &slv_smmu_jpeg_cfg.node, &slv_smmu_mdp_cfg.node, &slv_mnoc_mpu_cfg.node);
DEFINE_QNODE(mas_cnoc_mnoc_cfg, MASTER_CNOC_MNOC_CFG, 2, 1, 8, 1, ICBID_MASTER_CNOC_MNOC_CFG, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_srvc_mnoc.node);
DEFINE_QNODE(mas_cpp, MASTER_CPP, 5, 1, 32, 1, ICBID_MASTER_CPP, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_mnoc_bimc.node);
DEFINE_QNODE(mas_jpeg, MASTER_JPEG, 7, 1, 32, 1, ICBID_MASTER_JPEG, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_mnoc_bimc.node);
DEFINE_QNODE(mas_mdp_p0, MASTER_MDP_PORT0, 1, 1, 32, 1, ICBID_MASTER_MDP0, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_mnoc_bimc.node);
DEFINE_QNODE(mas_mdp_p1, MASTER_MDP_PORT1, 2, 1, 32, 1, ICBID_MASTER_MDP1, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_mnoc_bimc.node);
DEFINE_QNODE(mas_rotator, MASTER_ROTATOR, 0, 1, 32, 1, ICBID_MASTER_ROTATOR, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_mnoc_bimc.node);
DEFINE_QNODE(mas_venus, MASTER_VIDEO_P0, 3, 2, 32, 1, ICBID_MASTER_VIDEO, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_mnoc_bimc.node);
DEFINE_QNODE(mas_vfe, MASTER_VFE, 6, 1, 32, 1, ICBID_MASTER_VFE, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_mnoc_bimc.node);
DEFINE_QNODE(mas_snoc_vmem, MASTER_SNOC_VMEM, 6, 1, 32, 1, ICBID_MASTER_SNOC_VMEM, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_vmem.node);
DEFINE_QNODE(mas_venus_vmem, MASTER_VIDEO_P0_OCMEM, 6, 1, 32, 1, ICBID_MASTER_VENUS_VMEM, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_vmem.node);
DEFINE_QNODE(mas_snoc_pnoc, SNOC_PNOC_MAS, 6, 1, 8, 0, ICBID_MASTER_SNOC_PNOC, 0, QCOM_QOS_MODE_BYPASS, 9, &slv_blsp_1.node, &slv_blsp_2.node, &slv_usb_hs.node, &slv_sdcc_1.node, &slv_sdcc_2.node, &slv_sdcc_4.node, &slv_tsif.node, &slv_pdm.node, &slv_ahb2phy.node);
DEFINE_QNODE(mas_sdcc_1, MASTER_SDCC_1, 6, 1, 8, 0, ICBID_MASTER_SDCC_1, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_pnoc_a1noc.node);
DEFINE_QNODE(mas_sdcc_2, MASTER_SDCC_2, 6, 1, 8, 0, ICBID_MASTER_SDCC_2, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_pnoc_a1noc.node);
DEFINE_QNODE(mas_sdcc_4, MASTER_SDCC_4, 6, 1, 8, 0, ICBID_MASTER_SDCC_4, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_pnoc_a1noc.node);
DEFINE_QNODE(mas_usb_hs, MASTER_USB_HS, 6, 1, 8, 0, ICBID_MASTER_USB_HS, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_pnoc_a1noc.node);
DEFINE_QNODE(mas_blsp_1, MASTER_BLSP_1, 6, 1, 4, 0, ICBID_MASTER_BLSP_1, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_pnoc_a1noc.node);
DEFINE_QNODE(mas_blsp_2, MASTER_BLSP_2, 6, 1, 4, 0, ICBID_MASTER_BLSP_2, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_pnoc_a1noc.node);
DEFINE_QNODE(mas_tsif, MASTER_TSIF, 6, 1, 4, 0, ICBID_MASTER_TSIF, 0, QCOM_QOS_MODE_BYPASS, 1, &slv_pnoc_a1noc.node);
DEFINE_QNODE(mas_hmss, MASTER_HMSS, 4, 1, 8, 1, ICBID_MASTER_HMSS, 0, QCOM_QOS_MODE_FIXED, 3, &slv_pimem.node, &slv_imem.node, &slv_snoc_bimc.node);
DEFINE_QNODE(mas_qdss_bam, MASTER_QDSS_BAM, 2, 1, 16, 1, ICBID_MASTER_QDSS_BAM, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(mas_snoc_cfg, MASTER_SNOC_CFG, 2, 1, 16, 1, ICBID_MASTER_SNOC_CFG, 0, QCOM_QOS_MODE_FIXED, 1, &slv_srvc_snoc.node);
DEFINE_QNODE(mas_bimc_snoc_0, BIMC_SNOC_MAS, 2, 1, 16, 1, ICBID_MASTER_BIMC_SNOC, 0, QCOM_QOS_MODE_FIXED, 9, &slv_snoc_vmem.node, &slv_usb3.node, &slv_pimem.node, &slv_lpass.node, &slv_hmss.node, &slv_snoc_cnoc.node, &slv_snoc_pnoc.node, &slv_imem.node, &slv_qdss_stm.node);
DEFINE_QNODE(mas_bimc_snoc_1, BIMC_SNOC_1_MAS, 2, 1, 16, 1, ICBID_MASTER_BIMC_SNOC_1, 0, QCOM_QOS_MODE_FIXED, 3, &slv_pcie_2.node, &slv_pcie_1.node, &slv_pcie_0.node);
DEFINE_QNODE(mas_a0noc_snoc, A0NOC_SNOC_MAS, 2, 1, 16, 1, ICBID_MASTER_A0NOC_SNOC, 0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(mas_a1noc_snoc, A1NOC_SNOC_MAS, 2, 1, 16, 0, ICBID_MASTER_A1NOC_SNOC, 0, QCOM_QOS_MODE_FIXED, 13, &slv_snoc_vmem.node, &slv_usb3.node, &slv_pcie_0.node, &slv_pimem.node, &slv_pcie_2.node, &slv_lpass.node, &slv_pcie_1.node, &slv_hmss.node, &slv_snoc_bimc.node, &slv_snoc_cnoc.node, &slv_snoc_pnoc.node, &slv_imem.node, &slv_qdss_stm.node);
DEFINE_QNODE(mas_a2noc_snoc, A2NOC_SNOC_MAS, 2, 1, 16, 0, ICBID_MASTER_A2NOC_SNOC, 0, QCOM_QOS_MODE_FIXED, 12, &slv_snoc_vmem.node, &slv_usb3.node, &slv_pcie_1.node, &slv_pimem.node, &slv_pcie_2.node, &slv_qdss_stm.node, &slv_lpass.node, &slv_snoc_bimc.node, &slv_snoc_cnoc.node, &slv_snoc_pnoc.node, &slv_imem.node, &slv_pcie_0.node);
DEFINE_QNODE(mas_qdss_etr, MASTER_QDSS_ETR, 3, 1, 16, 1, ICBID_MASTER_QDSS_ETR, 0, QCOM_QOS_MODE_FIXED, 5, &slv_pimem.node, &slv_usb3.node, &slv_imem.node, &slv_snoc_bimc.node, &slv_snoc_pnoc.node);
DEFINE_QNODE(slv_a0noc_snoc, A0NOC_SNOC_SLV, 3, 1, 8, 1, 0, ICBID_SLAVE_A0NOC_SNOC, QCOM_QOS_MODE_FIXED, 1, &mas_a0noc_snoc.node);
DEFINE_QNODE(slv_a1noc_snoc, A1NOC_SNOC_SLV, 3, 1, 8, 0, 0, ICBID_SLAVE_A1NOC_SNOC, QCOM_QOS_MODE_FIXED, 1, &mas_a1noc_snoc.node);
DEFINE_QNODE(slv_a2noc_snoc, A2NOC_SNOC_SLV, 3, 1, 8, 0, 0, ICBID_SLAVE_A2NOC_SNOC, QCOM_QOS_MODE_FIXED, 1, &mas_a2noc_snoc.node);
DEFINE_QNODE(slv_ebi, SLAVE_EBI_CH0, 3, 2, 8, 0, 0, ICBID_SLAVE_EBI1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_hmss_l3, SLAVE_HMSS_L3, 3, 1, 8, 0, 0, ICBID_SLAVE_HMSS_L3, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_bimc_snoc_0, BIMC_SNOC_SLV, 3, 1, 8, 1, 0, ICBID_SLAVE_BIMC_SNOC, QCOM_QOS_MODE_FIXED, 1, &mas_bimc_snoc_0.node);
DEFINE_QNODE(slv_bimc_snoc_1, BIMC_SNOC_1_SLV, 3, 1, 8, 1, 0, ICBID_SLAVE_BIMC_SNOC_1, QCOM_QOS_MODE_FIXED, 1, &mas_bimc_snoc_1.node);
DEFINE_QNODE(slv_cnoc_a1noc, CNOC_SNOC_SLV, 3, 1, 4, 1, 0, ICBID_SLAVE_CNOC_SNOC, QCOM_QOS_MODE_FIXED, 1, &mas_cnoc_a1noc.node);
DEFINE_QNODE(slv_clk_ctl, SLAVE_CLK_CTL, 3, 1, 4, 0, 0, ICBID_SLAVE_CLK_CTL, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_tcsr, SLAVE_TCSR, 3, 1, 4, 0, 0, ICBID_SLAVE_TCSR, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_tlmm, SLAVE_TLMM, 3, 1, 4, 0, 0, ICBID_SLAVE_TLMM, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_crypto0_cfg, SLAVE_CRYPTO_0_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_CRYPTO_0_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_mpm, SLAVE_MPM, 3, 1, 4, 1, 0, ICBID_SLAVE_MPM, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pimem_cfg, SLAVE_PIMEM_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_PIMEM_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_imem_cfg, SLAVE_IMEM_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_IMEM_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_message_ram, SLAVE_MESSAGE_RAM, 3, 1, 4, 0, 0, ICBID_SLAVE_MESSAGE_RAM, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_bimc_cfg, SLAVE_BIMC_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_BIMC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pmic_arb, SLAVE_PMIC_ARB, 3, 1, 4, 0, 0, ICBID_SLAVE_PMIC_ARB, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_prng, SLAVE_PRNG, 3, 1, 4, 1, 0, ICBID_SLAVE_PRNG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_dcc_cfg, SLAVE_DCC_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_DCC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_rbcpr_mx, SLAVE_RBCPR_MX, 3, 1, 4, 1, 0, ICBID_SLAVE_RBCPR_MX, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_qdss_cfg, SLAVE_QDSS_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_QDSS_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_rbcpr_cx, SLAVE_RBCPR_CX, 3, 1, 4, 1, 0, ICBID_SLAVE_RBCPR_CX, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_cpr_apu_cfg, SLAVE_QDSS_RBCPR_APU_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_QDSS_RBCPR_APU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_cnoc_mnoc_cfg, SLAVE_CNOC_MNOC_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_CNOC_MNOC_CFG, QCOM_QOS_MODE_FIXED, 1, &mas_cnoc_mnoc_cfg.node);
DEFINE_QNODE(slv_snoc_cfg, SLAVE_SNOC_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_SNOC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_snoc_mpu_cfg, SLAVE_SNOC_MPU_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_SNOC_MPU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_ebi1_phy_cfg, SLAVE_EBI1_PHY_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_EBI1_PHY_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a0noc_cfg, SLAVE_A0NOC_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_A0NOC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pcie_1_cfg, SLAVE_PCIE_1_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_PCIE_1_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pcie_2_cfg, SLAVE_PCIE_2_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_PCIE_2_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pcie_0_cfg, SLAVE_PCIE_0_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_PCIE_0_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pcie20_ahb2phy, SLAVE_PCIE20_AHB2PHY, 3, 1, 4, 1, 0, ICBID_SLAVE_PCIE20_AHB2PHY, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a0noc_mpu_cfg, SLAVE_A0NOC_MPU_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_A0NOC_MPU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_ufs_cfg, SLAVE_UFS_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_UFS_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a1noc_cfg, SLAVE_A1NOC_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_A1NOC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a1noc_mpu_cfg, SLAVE_A1NOC_MPU_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_A1NOC_MPU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a2noc_cfg, SLAVE_A2NOC_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_A2NOC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a2noc_mpu_cfg, SLAVE_A2NOC_MPU_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_A2NOC_MPU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_ssc_cfg, SLAVE_SSC_CFG, 3, 1, 4, 1, 0, ICBID_SLAVE_SSC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a0noc_smmu_cfg, SLAVE_A0NOC_SMMU_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_A0NOC_SMMU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a1noc_smmu_cfg, SLAVE_A1NOC_SMMU_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_A1NOC_SMMU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_a2noc_smmu_cfg, SLAVE_A2NOC_SMMU_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_A2NOC_SMMU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_lpass_smmu_cfg, SLAVE_LPASS_SMMU_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_LPASS_SMMU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_cnoc_mnoc_mmss_cfg, SLAVE_CNOC_MNOC_MMSS_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_CNOC_MNOC_MMSS_CFG, QCOM_QOS_MODE_FIXED, 1, &mas_cnoc_mnoc_mmss_cfg.node);
DEFINE_QNODE(slv_mmagic_cfg, SLAVE_MMAGIC_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_MMAGIC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_cpr_cfg, SLAVE_CPR_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_CPR_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_misc_cfg, SLAVE_MISC_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_MISC_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_venus_throttle_cfg, SLAVE_VENUS_THROTTLE_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_VENUS_THROTTLE_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_venus_cfg, SLAVE_VENUS_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_VENUS_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_vmem_cfg, SLAVE_VMEM_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_VMEM_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_dsa_cfg, SLAVE_DSA_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_DSA_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_mnoc_clocks_cfg, SLAVE_MMSS_CLK_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_MMSS_CLK_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_dsa_mpu_cfg, SLAVE_DSA_MPU_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_DSA_MPU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_mnoc_mpu_cfg, SLAVE_MNOC_MPU_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_MNOC_MPU_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_display_cfg, SLAVE_DISPLAY_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_DISPLAY_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_display_throttle_cfg, SLAVE_DISPLAY_THROTTLE_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_DISPLAY_THROTTLE_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_camera_cfg, SLAVE_CAMERA_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_CAMERA_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_camera_throttle_cfg, SLAVE_CAMERA_THROTTLE_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_CAMERA_THROTTLE_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_oxili_cfg, SLAVE_GRAPHICS_3D_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_GFX3D_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_smmu_mdp_cfg, SLAVE_SMMU_MDP_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_SMMU_MDP_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_smmu_rot_cfg, SLAVE_SMMU_ROTATOR_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_SMMU_ROTATOR_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_smmu_venus_cfg, SLAVE_SMMU_VENUS_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_SMMU_VENUS_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_smmu_cpp_cfg, SLAVE_SMMU_CPP_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_SMMU_CPP_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_smmu_jpeg_cfg, SLAVE_SMMU_JPEG_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_SMMU_JPEG_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_smmu_vfe_cfg, SLAVE_SMMU_VFE_CFG, 3, 1, 8, 1, 0, ICBID_SLAVE_SMMU_VFE_CFG, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_mnoc_bimc, MNOC_BIMC_SLV, 3, 2, 32, 1, 0, ICBID_SLAVE_MNOC_BIMC, QCOM_QOS_MODE_FIXED, 1, &mas_mnoc_bimc.node);
DEFINE_QNODE(slv_vmem, SLAVE_VMEM, 3, 1, 32, 1, 0, ICBID_SLAVE_VMEM, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_srvc_mnoc, SLAVE_SERVICE_MNOC, 3, 1, 8, 1, 0, ICBID_SLAVE_SERVICE_MNOC, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pnoc_a1noc, PNOC_A1NOC_SLV, 3, 1, 8, 0, 0, ICBID_SLAVE_PNOC_A1NOC, QCOM_QOS_MODE_FIXED, 1, &mas_pnoc_a1noc.node);
DEFINE_QNODE(slv_usb_hs, SLAVE_USB_HS, 3, 1, 4, 0, 0, ICBID_SLAVE_USB_HS, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_sdcc_2, SLAVE_SDCC_2, 3, 1, 4, 0, 0, ICBID_SLAVE_SDCC_2, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_sdcc_4, SLAVE_SDCC_4, 3, 1, 4, 0, 0, ICBID_SLAVE_SDCC_4, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_tsif, SLAVE_TSIF, 3, 1, 4, 0, 0, ICBID_SLAVE_TSIF, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_blsp_2, SLAVE_BLSP_2, 3, 1, 4, 0, 0, ICBID_SLAVE_BLSP_2, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_sdcc_1, SLAVE_SDCC_1, 3, 1, 4, 0, 0, ICBID_SLAVE_SDCC_1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_blsp_1, SLAVE_BLSP_1, 3, 1, 4, 0, 0, ICBID_SLAVE_BLSP_1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pdm, SLAVE_PDM, 3, 1, 4, 0, 0, ICBID_SLAVE_PDM, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_ahb2phy, SLAVE_AHB2PHY, 3, 1, 4, 1, 0, ICBID_SLAVE_AHB2PHY, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_hmss, SLAVE_APPSS, 3, 1, 16, 1, 0, ICBID_SLAVE_APPSS, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_lpass, SLAVE_LPASS, 3, 1, 16, 1, 0, ICBID_SLAVE_LPASS, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_usb3, SLAVE_USB3, 3, 1, 16, 1, 0, ICBID_SLAVE_USB3_0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_snoc_bimc, SNOC_BIMC_SLV, 3, 2, 32, 0, 0, ICBID_SLAVE_SNOC_BIMC, QCOM_QOS_MODE_FIXED, 1, &mas_snoc_bimc.node);
DEFINE_QNODE(slv_snoc_cnoc, SNOC_CNOC_SLV, 3, 1, 16, 0, 0, ICBID_SLAVE_SNOC_CNOC, QCOM_QOS_MODE_FIXED, 1, &mas_snoc_cnoc.node);
DEFINE_QNODE(slv_imem, SLAVE_OCIMEM, 3, 1, 16, 0, 0, ICBID_SLAVE_IMEM, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pimem, SLAVE_PIMEM, 3, 1, 16, 0, 0, ICBID_SLAVE_PIMEM, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_snoc_vmem, SLAVE_SNOC_VMEM, 3, 1, 16, 1, 0, ICBID_SLAVE_SNOC_VMEM, QCOM_QOS_MODE_FIXED, 1, &mas_snoc_vmem.node);
DEFINE_QNODE(slv_snoc_pnoc, SNOC_PNOC_SLV, 3, 1, 16, 0, 0, ICBID_SLAVE_SNOC_PNOC, QCOM_QOS_MODE_FIXED, 1, &mas_snoc_pnoc.node);
DEFINE_QNODE(slv_qdss_stm, SLAVE_QDSS_STM, 3, 1, 16, 0, 0, ICBID_SLAVE_QDSS_STM, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pcie_0, SLAVE_PCIE_0, 3, 1, 16, 1, 0, ICBID_SLAVE_PCIE_0, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pcie_1, SLAVE_PCIE_1, 3, 1, 16, 1, 0, ICBID_SLAVE_PCIE_1, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_pcie_2, SLAVE_PCIE_2, 3, 1, 16, 1, 0, ICBID_SLAVE_PCIE_2, QCOM_QOS_MODE_FIXED, 0, 0);
DEFINE_QNODE(slv_srvc_snoc, SLAVE_SERVICE_SNOC, 3, 1, 16, 1, 0, ICBID_SLAVE_SERVICE_SNOC, QCOM_QOS_MODE_FIXED, 0, 0);

static struct qcom_interconnect_node *msm8996_snoc_nodes[] = {
	&mas_hmss,
	&mas_qdss_bam,
	&mas_snoc_cfg,
	&mas_bimc_snoc_0,
	&mas_bimc_snoc_1,
	&mas_a0noc_snoc,
	&mas_a1noc_snoc,
	&mas_a2noc_snoc,
	&mas_qdss_etr,
	&slv_a0noc_snoc,
	&slv_a1noc_snoc,
	&slv_a2noc_snoc,
	&slv_hmss,
	&slv_lpass,
	&slv_usb3,
	&slv_snoc_bimc,
	&slv_snoc_cnoc,
	&slv_imem,
	&slv_pimem,
	&slv_snoc_vmem,
	&slv_snoc_pnoc,
	&slv_qdss_stm,
	&slv_pcie_0,
	&slv_pcie_1,
	&slv_pcie_2,
	&slv_srvc_snoc,
};

static struct qcom_interconnect_desc msm8996_snoc = {
	.nodes = msm8996_snoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_snoc_nodes),
};

static struct qcom_interconnect_node *msm8996_bimc_nodes[] = {
	&mas_apps_proc,
	&mas_oxili,
	&mas_mnoc_bimc,
	&mas_snoc_bimc,
	&slv_ebi,
	&slv_hmss_l3,
	&slv_bimc_snoc_0,
	&slv_bimc_snoc_1,
};

static struct qcom_interconnect_desc msm8996_bimc = {
	.nodes = msm8996_bimc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_bimc_nodes),
};

static struct qcom_interconnect_node *msm8996_pnoc_nodes[] = {
	&mas_snoc_pnoc,
	&mas_sdcc_1,
	&mas_sdcc_2,
	&mas_sdcc_4,
	&mas_usb_hs,
	&mas_blsp_1,
	&mas_blsp_2,
	&mas_tsif,
	&slv_pnoc_a1noc,
	&slv_usb_hs,
	&slv_sdcc_2,
	&slv_sdcc_4,
	&slv_tsif,
	&slv_blsp_2,
	&slv_sdcc_1,
	&slv_blsp_1,
	&slv_pdm,
	&slv_ahb2phy,
};

static struct qcom_interconnect_desc msm8996_pnoc = {
	.nodes = msm8996_pnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_pnoc_nodes),
};

static struct qcom_interconnect_node *msm8996_cnoc_nodes[] = {
	&mas_snoc_cnoc,
	&mas_qdss_dap,
	&slv_cnoc_a1noc,
	&slv_clk_ctl,
	&slv_tcsr,
	&slv_tlmm,
	&slv_crypto0_cfg,
	&slv_mpm,
	&slv_pimem_cfg,
	&slv_imem_cfg,
	&slv_message_ram,
	&slv_bimc_cfg,
	&slv_pmic_arb,
	&slv_prng,
	&slv_dcc_cfg,
	&slv_rbcpr_mx,
	&slv_qdss_cfg,
	&slv_rbcpr_cx,
	&slv_cpr_apu_cfg,
	&slv_cnoc_mnoc_cfg,
	&slv_snoc_cfg,
	&slv_snoc_mpu_cfg,
	&slv_ebi1_phy_cfg,
	&slv_a0noc_cfg,
	&slv_pcie_1_cfg,
	&slv_pcie_2_cfg,
	&slv_pcie_0_cfg,
	&slv_pcie20_ahb2phy,
	&slv_a0noc_mpu_cfg,
	&slv_ufs_cfg,
	&slv_a1noc_cfg,
	&slv_a1noc_mpu_cfg,
	&slv_a2noc_cfg,
	&slv_a2noc_mpu_cfg,
	&slv_ssc_cfg,
	&slv_a0noc_smmu_cfg,
	&slv_a1noc_smmu_cfg,
	&slv_a2noc_smmu_cfg,
	&slv_lpass_smmu_cfg,
	&slv_cnoc_mnoc_mmss_cfg,
};

static struct qcom_interconnect_desc msm8996_cnoc = {
	.nodes = msm8996_cnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_cnoc_nodes),
};

static struct qcom_interconnect_node *msm8996_mnoc_nodes[] = {
	&mas_cnoc_mnoc_mmss_cfg,
	&mas_cnoc_mnoc_cfg,
	&mas_cpp,
	&mas_jpeg,
	&mas_mdp_p0,
	&mas_mdp_p1,
	&mas_rotator,
	&mas_venus,
	&mas_vfe,
	&mas_snoc_vmem,
	&mas_venus_vmem,
	&slv_mmagic_cfg,
	&slv_cpr_cfg,
	&slv_misc_cfg,
	&slv_venus_throttle_cfg,
	&slv_venus_cfg,
	&slv_vmem_cfg,
	&slv_dsa_cfg,
	&slv_mnoc_clocks_cfg,
	&slv_dsa_mpu_cfg,
	&slv_mnoc_mpu_cfg,
	&slv_display_cfg,
	&slv_display_throttle_cfg,
	&slv_camera_cfg,
	&slv_camera_throttle_cfg,
	&slv_oxili_cfg,
	&slv_smmu_mdp_cfg,
	&slv_smmu_rot_cfg,
	&slv_smmu_venus_cfg,
	&slv_smmu_cpp_cfg,
	&slv_smmu_jpeg_cfg,
	&slv_smmu_vfe_cfg,
	&slv_mnoc_bimc,
	&slv_vmem,
	&slv_srvc_mnoc,
};

static struct qcom_interconnect_desc msm8996_mnoc = {
	.nodes = msm8996_mnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_mnoc_nodes),
};

static struct qcom_interconnect_node *msm8996_a0noc_nodes[] = {
	&mas_pcie_0,
	&mas_pcie_1,
	&mas_pcie_2,
};

static struct qcom_interconnect_desc msm8996_a0noc = {
	.nodes = msm8996_a0noc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_a0noc_nodes),
};

static struct qcom_interconnect_node *msm8996_a1noc_nodes[] = {
	&mas_cnoc_a1noc,
	&mas_crypto_c0,
	&mas_pnoc_a1noc,
};

static struct qcom_interconnect_desc msm8996_a1noc = {
	.nodes = msm8996_a1noc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_a1noc_nodes),
};

static struct qcom_interconnect_node *msm8996_a2noc_nodes[] = {
	&mas_usb3,
	&mas_ipa,
	&mas_ufs,
};

static struct qcom_interconnect_desc msm8996_a2noc = {
	.nodes = msm8996_a2noc_nodes,
	.num_nodes = ARRAY_SIZE(msm8996_a2noc_nodes),
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
	{ .compatible = "qcom,msm8996-bimc", .data = &msm8996_bimc },
	{ .compatible = "qcom,msm8996-cnoc", .data = &msm8996_cnoc },
	{ .compatible = "qcom,msm8996-snoc", .data = &msm8996_snoc },
	{ .compatible = "qcom,msm8996-a0noc", .data = &msm8996_a0noc },
	{ .compatible = "qcom,msm8996-a1noc", .data = &msm8996_a1noc },
	{ .compatible = "qcom,msm8996-a2noc", .data = &msm8996_a2noc },
	{ .compatible = "qcom,msm8996-mmnoc", .data = &msm8996_mnoc },
	{ .compatible = "qcom,msm8996-pnoc", .data = &msm8996_pnoc },
	{ },
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.driver = {
		.name = "qnoc-msm8996",
		.of_match_table = qnoc_of_match,
	},
};
module_platform_driver(qnoc_driver);
MODULE_AUTHOR("Georgi Djakov <georgi.djakov@linaro.org>");
MODULE_DESCRIPTION("Qualcomm msm8996 NoC driver");
MODULE_LICENSE("GPL v2");
