// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Google, Inc. */

#include <linux/types.h>
#include <linux/debugfs.h>
#include <drm/drm_print.h>

#include "a6xx_gpu.h"

static int sqe_stat_print(struct msm_gpu *gpu, struct drm_printer *p)
{
	int i;

	drm_printf(p, "SQE state:\n");
	gpu_write(gpu, REG_A6XX_CP_SQE_STAT_ADDR, 0);

	for (i = 0; i < 0x33; i++) {
		drm_printf(p, "  %02x: %08x\n", i,
			gpu_read(gpu, REG_A6XX_CP_SQE_STAT_DATA));
	}

	return 0;
}

static int sqe_dbg_print(struct msm_gpu *gpu, struct drm_printer *p)
{
	int i;

	drm_printf(p, "SQE ucode debug:\n");
	gpu_write(gpu, REG_A6XX_CP_SQE_UCODE_DBG_ADDR, 0);

	for (i = 0; i < 0x6000 / 8; i++) {
		uint32_t val[8];
		int j;
		for (j = 0; j < 8; j++)
			val[j] = gpu_read(gpu, REG_A6XX_CP_SQE_UCODE_DBG_DATA);
		drm_printf(p, "  %04x: %08x %08x %08x %08x   %08x %08x %08x %08x\n", i * 8,
			val[0], val[1], val[2], val[3], val[4], val[5], val[6], val[7]);
	}

	return 0;
}

static int show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	struct drm_printer p = drm_seq_file_printer(m);
	int (*show)(struct msm_gpu *gpu, struct drm_printer *p) =
		node->info_ent->data;
	int ret;

	pm_runtime_get_sync(&gpu->pdev->dev);
	ret = show(priv->gpu, &p);
	pm_runtime_put_sync(&gpu->pdev->dev);

	return ret;
}

#define ENT(n) { .name = #n, .show = show, .data = n ##_print }
static struct drm_info_list a6xx_debugfs_list[] = {
	ENT(sqe_stat),
	ENT(sqe_dbg),
};

/* for debugfs files that can be written to, we can't use drm helper: */
static int
reset_set(void *data, u64 val)
{
	struct drm_device *dev = data;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	if (!capable(CAP_SYS_ADMIN))
		return -EINVAL;

	/* TODO do we care about trying to make sure the GPU is idle?
	 * Since this is just a debug feature limited to CAP_SYS_ADMIN,
	 * maybe it is fine to let the user keep both pieces if they
	 * try to reset an active GPU.
	 */

	mutex_lock(&dev->struct_mutex);

	release_firmware(adreno_gpu->fw[ADRENO_FW_SQE]);
	adreno_gpu->fw[ADRENO_FW_SQE] = NULL;

	release_firmware(adreno_gpu->fw[ADRENO_FW_GMU]);
	adreno_gpu->fw[ADRENO_FW_GMU] = NULL;

	if (a6xx_gpu->sqe_bo) {
		msm_gem_unpin_iova(a6xx_gpu->sqe_bo, gpu->aspace);
		drm_gem_object_put(a6xx_gpu->sqe_bo);
		a6xx_gpu->sqe_bo = NULL;
	}

	gpu->needs_hw_init = true;

	pm_runtime_get_sync(&gpu->pdev->dev);
	gpu->funcs->recover(gpu);
	pm_runtime_put_sync(&gpu->pdev->dev);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(reset_fops, NULL, reset_set, "%llx\n");


int a6xx_debugfs_init(struct msm_gpu *gpu, struct drm_minor *minor)
{
	struct drm_device *dev;
	struct dentry *ent;
	int ret;

	if (!minor)
		return 0;

	dev = minor->dev;

	ret = drm_debugfs_create_files(a6xx_debugfs_list,
			ARRAY_SIZE(a6xx_debugfs_list),
			minor->debugfs_root, minor);

	if (ret) {
		DRM_DEV_ERROR(dev->dev, "could not install a6xx_debugfs_list\n");
		return ret;
	}

	ent = debugfs_create_file("reset", S_IWUGO,
		minor->debugfs_root,
		dev, &reset_fops);
	if (!ent)
		return -ENOMEM;

	return 0;
}
