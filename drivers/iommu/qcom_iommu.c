/*
 * IOMMU API for QCOM secure IOMMUs.  Somewhat based on arm-smmu.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2013 ARM Limited
 * Copyright (C) 2017 Red Hat
 */

#define pr_fmt(fmt) "qcom-iommu: " fmt

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_iommu.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/qcom_scm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "io-pgtable.h"
#include "arm-smmu-regs.h"

#define SMMU_INTR_SEL_NS     0x2000


struct qcom_iommu_device {
	/* IOMMU core code handle */
	struct iommu_device	 iommu;

	struct device		*dev;

	void __iomem		*base;
	void __iomem		*local_base;
	unsigned int		 irq;
	struct clk		*iface_clk;
	struct clk		*bus_clk;

	bool			 secure_init;
	u32			 asid;      /* asid and ctx bank # are 1:1 */
	u32			 sec_id;

	/* single group per device: */
	struct iommu_group	*group;
};

struct qcom_iommu_domain {
	struct qcom_iommu_device	*iommu;
	struct io_pgtable_ops		*pgtbl_ops;
	spinlock_t			 pgtbl_lock;
	struct mutex			 init_mutex; /* Protects iommu pointer */
	struct iommu_domain		 domain;
};

static struct qcom_iommu_domain *to_qcom_iommu_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct qcom_iommu_domain, domain);
}

static const struct iommu_ops qcom_iommu_ops;
static struct platform_driver qcom_iommu_driver;

static struct qcom_iommu_device * dev_to_iommu(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev->iommu_fwspec;
	if (WARN_ON(!fwspec || fwspec->ops != &qcom_iommu_ops))
		return NULL;
	return fwspec->iommu_priv;
}

static inline void
iommu_writel(struct qcom_iommu_device *qcom_iommu, unsigned reg, u32 val)
{
	writel_relaxed(val, qcom_iommu->base + reg);
}

static inline void
iommu_writeq(struct qcom_iommu_device *qcom_iommu, unsigned reg, u64 val)
{
	writeq_relaxed(val, qcom_iommu->base + reg);
}

static inline u32
iommu_readl(struct qcom_iommu_device *qcom_iommu, unsigned reg)
{
	return readl_relaxed(qcom_iommu->base + reg);
}

static inline u32
iommu_readq(struct qcom_iommu_device *qcom_iommu, unsigned reg)
{
	return readq_relaxed(qcom_iommu->base + reg);
}

static void __sync_tlb(struct qcom_iommu_device *qcom_iommu)
{
	unsigned int val;
	unsigned int ret;

	iommu_writel(qcom_iommu, ARM_SMMU_CB_TLBSYNC, 0);

	ret = readl_poll_timeout(qcom_iommu->base + ARM_SMMU_CB_TLBSTATUS, val,
				 (val & 0x1) == 0, 0, 5000000);
	if (ret)
		dev_err(qcom_iommu->dev, "timeout waiting for TLB SYNC\n");
}


static void qcom_iommu_tlb_sync(void *cookie)
{
	struct qcom_iommu_device *qcom_iommu = cookie;
	__sync_tlb(qcom_iommu);
}

static void qcom_iommu_tlb_inv_context(void *cookie)
{
	struct qcom_iommu_device *qcom_iommu = cookie;

	iommu_writel(qcom_iommu, ARM_SMMU_CB_S1_TLBIASID, qcom_iommu->asid);
	__sync_tlb(qcom_iommu);
}

static void qcom_iommu_tlb_inv_range_nosync(unsigned long iova, size_t size,
					    size_t granule, bool leaf, void *cookie)
{
	struct qcom_iommu_device *qcom_iommu = cookie;
	unsigned reg;

	reg = leaf ? ARM_SMMU_CB_S1_TLBIVAL : ARM_SMMU_CB_S1_TLBIVA;

	/* TODO do we need to support aarch64 fmt too? */

	iova &= ~12UL;
	iova |= qcom_iommu->asid;
	do {
		iommu_writel(qcom_iommu, reg, iova);
		iova += granule;
	} while (size -= granule);
}

static const struct iommu_gather_ops qcom_gather_ops = {
	.tlb_flush_all	= qcom_iommu_tlb_inv_context,
	.tlb_add_flush	= qcom_iommu_tlb_inv_range_nosync,
	.tlb_sync	= qcom_iommu_tlb_sync,
};

static irqreturn_t qcom_iommu_fault(int irq, void *dev)
{
	struct qcom_iommu_device *qcom_iommu = dev;
	u32 fsr, fsynr;
	unsigned long iova;

	fsr = iommu_readl(qcom_iommu, ARM_SMMU_CB_FSR);

	if (!(fsr & FSR_FAULT))
		return IRQ_NONE;

	fsynr = iommu_readl(qcom_iommu, ARM_SMMU_CB_FSYNR0);
	iova = iommu_readq(qcom_iommu, ARM_SMMU_CB_FAR);

	dev_err_ratelimited(qcom_iommu->dev,
			    "Unhandled context fault: fsr=0x%x, "
			    "iova=0x%08lx, fsynr=0x%x, cb=%d\n",
			    fsr, iova, fsynr, qcom_iommu->asid);

	iommu_writel(qcom_iommu, ARM_SMMU_CB_FSR, fsr);

	return IRQ_HANDLED;
}

static int qcom_iommu_sec_init(struct qcom_iommu_device *qcom_iommu)
{
	if (qcom_iommu->local_base) {
		writel_relaxed(0xffffffff, qcom_iommu->local_base + SMMU_INTR_SEL_NS);
		mb();
	}

	return qcom_scm_restore_sec_cfg(qcom_iommu->sec_id, qcom_iommu->asid);
}


static int qcom_iommu_init_domain_context(struct iommu_domain *domain,
					  struct qcom_iommu_device *qcom_iommu)
{
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct io_pgtable_ops *pgtbl_ops;
	struct io_pgtable_cfg pgtbl_cfg;
	int ret = 0;
	u32 reg;

	mutex_lock(&qcom_domain->init_mutex);
	if (qcom_domain->iommu)
		goto out_unlock;

	/*
	 * TODO do we need to make the pagetable format configurable to
	 * support other devices?  Is deciding based on compat string
	 * sufficient?
	 */

	pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= qcom_iommu_ops.pgsize_bitmap,
		.ias		= 32,
		.oas		= 40,
		.tlb		= &qcom_gather_ops,
		.iommu_dev	= qcom_iommu->dev,
	};

	qcom_domain->iommu = qcom_iommu;
	pgtbl_ops = alloc_io_pgtable_ops(ARM_32_LPAE_S1, &pgtbl_cfg, qcom_iommu);
	if (!pgtbl_ops) {
		dev_err(qcom_iommu->dev, "failed to allocate pagetable ops\n");
		ret = -ENOMEM;
		goto out_clear_iommu;
	}

	/* Update the domain's page sizes to reflect the page table format */
	domain->pgsize_bitmap = pgtbl_cfg.pgsize_bitmap;
	domain->geometry.aperture_end = (1UL << 48) - 1;
	domain->geometry.force_aperture = true;

	if (!qcom_iommu->secure_init) {
		ret = qcom_iommu_sec_init(qcom_iommu);
		if (ret) {
			dev_err(qcom_iommu->dev, "secure init failed: %d\n", ret);
			goto out_clear_iommu;
		}
		qcom_iommu->secure_init = true;
	}

	/* TTBRs */
	iommu_writeq(qcom_iommu, ARM_SMMU_CB_TTBR0,
		     pgtbl_cfg.arm_lpae_s1_cfg.ttbr[0] |
		     ((u64)qcom_iommu->asid << TTBRn_ASID_SHIFT));
	iommu_writeq(qcom_iommu, ARM_SMMU_CB_TTBR1,
		     pgtbl_cfg.arm_lpae_s1_cfg.ttbr[1] |
		     ((u64)qcom_iommu->asid << TTBRn_ASID_SHIFT));

	/* TTBCR */
	iommu_writel(qcom_iommu, ARM_SMMU_CB_TTBCR2,
		     (pgtbl_cfg.arm_lpae_s1_cfg.tcr >> 32) |
		     TTBCR2_SEP_UPSTREAM);
	iommu_writel(qcom_iommu, ARM_SMMU_CB_TTBCR,
		     pgtbl_cfg.arm_lpae_s1_cfg.tcr);

	/* MAIRs (stage-1 only) */
	iommu_writel(qcom_iommu, ARM_SMMU_CB_S1_MAIR0,
		     pgtbl_cfg.arm_lpae_s1_cfg.mair[0]);
	iommu_writel(qcom_iommu, ARM_SMMU_CB_S1_MAIR1,
		     pgtbl_cfg.arm_lpae_s1_cfg.mair[1]);

	/* SCTLR */
	reg = SCTLR_CFIE | SCTLR_CFRE | SCTLR_AFE | SCTLR_TRE | SCTLR_M |
		SCTLR_S1_ASIDPNE;
#ifdef __BIG_ENDIAN
	reg |= SCTLR_E;
#endif
	iommu_writel(qcom_iommu, ARM_SMMU_CB_SCTLR, reg);

	mutex_unlock(&qcom_domain->init_mutex);

	/* Publish page table ops for map/unmap */
	qcom_domain->pgtbl_ops = pgtbl_ops;

	return 0;

out_clear_iommu:
	qcom_domain->iommu = NULL;
out_unlock:
	mutex_unlock(&qcom_domain->init_mutex);
	return ret;
}

static struct iommu_domain *qcom_iommu_domain_alloc(unsigned type)
{
	struct qcom_iommu_domain *qcom_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED && type != IOMMU_DOMAIN_DMA)
		return NULL;
	/*
	 * Allocate the domain and initialise some of its data structures.
	 * We can't really do anything meaningful until we've added a
	 * master.
	 */
	qcom_domain = kzalloc(sizeof(*qcom_domain), GFP_KERNEL);
	if (!qcom_domain)
		return NULL;

	if (type == IOMMU_DOMAIN_DMA &&
	    iommu_get_dma_cookie(&qcom_domain->domain)) {
		kfree(qcom_domain);
		return NULL;
	}

	mutex_init(&qcom_domain->init_mutex);
	spin_lock_init(&qcom_domain->pgtbl_lock);

	return &qcom_domain->domain;
}

static void qcom_iommu_domain_free(struct iommu_domain *domain)
{
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct qcom_iommu_device *qcom_iommu = qcom_domain->iommu;

	if (!qcom_iommu)
		return;

	/*
	 * Free the domain resources. We assume that all devices have
	 * already been detached.
	 */
	iommu_put_dma_cookie(domain);

	/*
	 * Disable the context bank before freeing page table
	 */
	iommu_writel(qcom_iommu, ARM_SMMU_CB_SCTLR, 0);

	free_io_pgtable_ops(qcom_domain->pgtbl_ops);

	kfree(qcom_domain);
}

static int qcom_iommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct qcom_iommu_device *qcom_iommu = dev_to_iommu(dev);
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	int ret;


	if (!qcom_iommu) {
		dev_err(dev, "cannot attach to IOMMU, is it on the same bus?\n");
		return -ENXIO;
	}

	/* Ensure that the domain is finalised */
	pm_runtime_get_sync(qcom_iommu->dev);
	ret = qcom_iommu_init_domain_context(domain, qcom_iommu);
	pm_runtime_put_sync(qcom_iommu->dev);
	if (ret < 0)
		return ret;

	/*
	 * Sanity check the domain. We don't support domains across
	 * different IOMMUs.
	 */
	if (qcom_domain->iommu != qcom_iommu) {
		dev_err(dev, "cannot attach to IOMMU %s while already "
			"attached to domain on IOMMU %s\n",
			dev_name(qcom_domain->iommu->dev),
			dev_name(qcom_iommu->dev));
		return -EINVAL;
	}

	return 0;
}

static int qcom_iommu_map(struct iommu_domain *domain, unsigned long iova,
			  phys_addr_t paddr, size_t size, int prot)
{
	int ret;
	unsigned long flags;
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct io_pgtable_ops *ops = qcom_domain->pgtbl_ops;

	if (!ops)
		return -ENODEV;

	spin_lock_irqsave(&qcom_domain->pgtbl_lock, flags);
	ret = ops->map(ops, iova, paddr, size, prot);
	spin_unlock_irqrestore(&qcom_domain->pgtbl_lock, flags);
	return ret;
}

static size_t qcom_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			       size_t size)
{
	size_t ret;
	unsigned long flags;
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct io_pgtable_ops *ops = qcom_domain->pgtbl_ops;

	if (!ops)
		return 0;

	spin_lock_irqsave(&qcom_domain->pgtbl_lock, flags);
	ret = ops->unmap(ops, iova, size);
	spin_unlock_irqrestore(&qcom_domain->pgtbl_lock, flags);
	return ret;
}

static phys_addr_t qcom_iommu_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t iova)
{
	phys_addr_t ret;
	unsigned long flags;
	struct qcom_iommu_domain *qcom_domain = to_qcom_iommu_domain(domain);
	struct io_pgtable_ops *ops = qcom_domain->pgtbl_ops;

	if (!ops)
		return 0;

	spin_lock_irqsave(&qcom_domain->pgtbl_lock, flags);
	ret = ops->iova_to_phys(ops, iova);
	spin_unlock_irqrestore(&qcom_domain->pgtbl_lock, flags);

	return ret;
}

static bool qcom_iommu_capable(enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		/*
		 * Return true here as the SMMU can always send out coherent
		 * requests.
		 */
		return true;
	case IOMMU_CAP_NOEXEC:
		return true;
	default:
		return false;
	}
}

static int qcom_iommu_add_device(struct device *dev)
{
	struct qcom_iommu_device *qcom_iommu = dev_to_iommu(dev);
	struct iommu_group *group;
	struct device_link *link;

	if (!qcom_iommu)
		return -ENODEV;

	group = iommu_group_get_for_dev(dev);
	if (IS_ERR_OR_NULL(group))
		return PTR_ERR_OR_ZERO(group);

	iommu_group_put(group);
	iommu_device_link(&qcom_iommu->iommu, dev);

	/*
	 * Establish the link between iommu and master, so that the
	 * iommu gets runtime enabled/disabled as per the master's
	 * needs.
	 */
	link = device_link_add(dev, qcom_iommu->dev, DL_FLAG_PM_RUNTIME);
	if (!link) {
		dev_warn(qcom_iommu->dev, "Unable to create device link between %s and %s\n",
			 dev_name(qcom_iommu->dev), dev_name(dev));
		/* TODO fatal or ignore? */
	}

	return 0;
}

static void qcom_iommu_remove_device(struct device *dev)
{
	struct qcom_iommu_device *qcom_iommu = dev_to_iommu(dev);

	if (!qcom_iommu)
		return;

	iommu_group_remove_device(dev);
	iommu_device_unlink(&qcom_iommu->iommu, dev);
	iommu_fwspec_free(dev);
}

static struct iommu_group *qcom_iommu_device_group(struct device *dev)
{
	struct qcom_iommu_device *qcom_iommu = dev_to_iommu(dev);

	if (qcom_iommu->group)
		return iommu_group_ref_get(qcom_iommu->group);

	qcom_iommu->group = generic_device_group(dev);

	return qcom_iommu->group;
}

static int qcom_iommu_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	struct platform_device *iommu_pdev;
	u32 fwid = 0;

	if (args->args_count != 0) {
		dev_err(dev, "incorrect number of iommu params found for %s "
			"(found %d, expected 0)\n",
			args->np->full_name, args->args_count);
		return -EINVAL;
	}

	if (!dev->iommu_fwspec->iommu_priv) {
		iommu_pdev = of_find_device_by_node(args->np);
		if (WARN_ON(!iommu_pdev))
			return -EINVAL;

		dev->iommu_fwspec->iommu_priv = platform_get_drvdata(iommu_pdev);
	}

	return iommu_fwspec_add_ids(dev, &fwid, 1);
}

static const struct iommu_ops qcom_iommu_ops = {
	.capable		= qcom_iommu_capable,
	.domain_alloc		= qcom_iommu_domain_alloc,
	.domain_free		= qcom_iommu_domain_free,
	.attach_dev		= qcom_iommu_attach_dev,
	.map			= qcom_iommu_map,
	.unmap			= qcom_iommu_unmap,
	.map_sg			= default_iommu_map_sg,
	.iova_to_phys		= qcom_iommu_iova_to_phys,
	.add_device		= qcom_iommu_add_device,
	.remove_device		= qcom_iommu_remove_device,
	.device_group		= qcom_iommu_device_group,
	.of_xlate		= qcom_iommu_of_xlate,
	.pgsize_bitmap		= SZ_4K | SZ_64K | SZ_1M | SZ_16M,
};

static const struct of_device_id qcom_iommu_of_match[] = {
	{ .compatible = "qcom,msm-sec-iommu-context-bank" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, qcom_iommu_of_match);

static int qcom_iommu_enable_clocks(struct qcom_iommu_device *qcom_iommu)
{
	int ret;

	ret = clk_prepare_enable(qcom_iommu->iface_clk);
	if (ret) {
		dev_err(qcom_iommu->dev, "Couldn't enable iface_clk\n");
		return ret;
	}

	ret = clk_prepare_enable(qcom_iommu->bus_clk);
	if (ret) {
		dev_err(qcom_iommu->dev, "Couldn't enable bus_clk\n");
		clk_disable_unprepare(qcom_iommu->iface_clk);
		return ret;
	}

	return 0;
}

static void qcom_iommu_disable_clocks(struct qcom_iommu_device *qcom_iommu)
{
	clk_disable_unprepare(qcom_iommu->bus_clk);
	clk_disable_unprepare(qcom_iommu->iface_clk);
}

static int qcom_iommu_device_probe(struct platform_device *pdev)
{
	struct resource *res;
	resource_size_t ioaddr;
	struct qcom_iommu_device *qcom_iommu;
	struct device *dev = &pdev->dev;
	int ret;

	qcom_iommu = devm_kzalloc(dev, sizeof(*qcom_iommu), GFP_KERNEL);
	if (!qcom_iommu) {
		dev_err(dev, "failed to allocate qcom_iommu_device\n");
		return -ENOMEM;
	}
	qcom_iommu->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	qcom_iommu->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qcom_iommu->base))
		return PTR_ERR(qcom_iommu->base);
	ioaddr = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "smmu_local_base");
	if (res)
		qcom_iommu->local_base = devm_ioremap_resource(dev, res);

	qcom_iommu->irq = platform_get_irq(pdev, 0);
	if (qcom_iommu->irq < 0) {
		dev_err(dev, "failed to get irq\n");
		return -ENODEV;
	}

	ret = devm_request_irq(dev, qcom_iommu->irq,
			       qcom_iommu_fault,
			       IRQF_SHARED,
			       "qcom-iommu-fault",
			       qcom_iommu);
	if (ret) {
		dev_err(dev, "failed to request IRQ %u\n",
			qcom_iommu->irq);
		return ret;
	}

	qcom_iommu->iface_clk = devm_clk_get(dev, "iface_clk");
	if (IS_ERR(qcom_iommu->iface_clk)) {
		dev_err(dev, "failed to get iface_clk\n");
		return PTR_ERR(qcom_iommu->iface_clk);
	}

	qcom_iommu->bus_clk = devm_clk_get(dev, "bus_clk");
	if (IS_ERR(qcom_iommu->bus_clk)) {
		dev_err(dev, "failed to get bus_clk\n");
		return PTR_ERR(qcom_iommu->bus_clk);
	}

	if (of_property_read_u32(dev->of_node, "qcom,iommu-ctx-asid",
				 &qcom_iommu->asid)) {
		dev_err(dev, "missing qcom,iommu-ctx-asid property\n");
		return -ENODEV;
	}

	if (of_property_read_u32(dev->of_node, "qcom,iommu-secure-id",
				 &qcom_iommu->sec_id)) {
		dev_err(dev, "missing qcom,iommu-secure-id property\n");
		return -ENODEV;
	}

	ret = iommu_device_sysfs_add(&qcom_iommu->iommu, dev, NULL,
				     "smmu.%pa", &ioaddr);
	if (ret) {
		dev_err(dev, "Failed to register iommu in sysfs\n");
		return ret;
	}

	iommu_device_set_ops(&qcom_iommu->iommu, &qcom_iommu_ops);
	iommu_device_set_fwnode(&qcom_iommu->iommu, dev->fwnode);

	ret = iommu_device_register(&qcom_iommu->iommu);
	if (ret) {
		dev_err(dev, "Failed to register iommu\n");
		return ret;
	}

	platform_set_drvdata(pdev, qcom_iommu);
	pm_runtime_enable(dev);
	bus_set_iommu(&platform_bus_type, &qcom_iommu_ops);

	return 0;
}

static int qcom_iommu_device_remove(struct platform_device *pdev)
{
	pm_runtime_force_suspend(&pdev->dev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int qcom_iommu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_iommu_device *qcom_iommu = platform_get_drvdata(pdev);

	return qcom_iommu_enable_clocks(qcom_iommu);
}

static int qcom_iommu_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_iommu_device *qcom_iommu = platform_get_drvdata(pdev);

	qcom_iommu_disable_clocks(qcom_iommu);

	return 0;
}
#endif

static const struct dev_pm_ops qcom_iommu_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_iommu_suspend, qcom_iommu_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};


static struct platform_driver qcom_iommu_driver = {
	.driver	= {
		.name		= "qcom-iommu",
		.of_match_table	= of_match_ptr(qcom_iommu_of_match),
		.pm		= &qcom_iommu_pm_ops,
	},
	.probe	= qcom_iommu_device_probe,
	.remove	= qcom_iommu_device_remove,
};
module_platform_driver(qcom_iommu_driver);

IOMMU_OF_DECLARE(qcom_iommu, "qcom,msm8916-iommu-context-bank", NULL);

MODULE_DESCRIPTION("IOMMU API for QCOM IOMMU implementations");
MODULE_LICENSE("GPL v2");
