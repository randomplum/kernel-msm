/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
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

#include <linux/sync_file.h>
#include <linux/dma-fence.h>
#include "msm_drv.h"
#include "dpu_kms.h"
#include "dpu_fence.h"

#define TIMELINE_VAL_LENGTH		128

void *dpu_sync_get(uint64_t fd)
{
	/* force signed compare, fdget accepts an int argument */
	return (signed int)fd >= 0 ? sync_file_get_fence(fd) : NULL;
}

void dpu_sync_put(void *fence)
{
	if (fence)
		dma_fence_put(fence);
}

signed long dpu_sync_wait(void *fnc, long timeout_ms)
{
	struct dma_fence *fence = fnc;
	int rc;
	char timeline_str[TIMELINE_VAL_LENGTH];

	if (!fence)
		return -EINVAL;
	else if (dma_fence_is_signaled(fence))
		return timeout_ms ? msecs_to_jiffies(timeout_ms) : 1;

	rc = dma_fence_wait_timeout(fence, true,
				msecs_to_jiffies(timeout_ms));
	if (!rc || (rc == -EINVAL)) {
		if (fence->ops->timeline_value_str)
			fence->ops->timeline_value_str(fence,
					timeline_str, TIMELINE_VAL_LENGTH);

		DPU_ERROR(
			"fence driver name:%s timeline name:%s seqno:0x%x timeline:%s signaled:0x%x\n",
			fence->ops->get_driver_name(fence),
			fence->ops->get_timeline_name(fence),
			fence->seqno, timeline_str,
			fence->ops->signaled ?
				fence->ops->signaled(fence) : 0xffffffff);
	}

	return rc;
}

uint32_t dpu_sync_get_name_prefix(void *fence)
{
	const char *name;
	uint32_t i, prefix;
	struct dma_fence *f = fence;

	if (!fence)
		return 0;

	name = f->ops->get_driver_name(f);
	if (!name)
		return 0;

	prefix = 0x0;
	for (i = 0; i < sizeof(uint32_t) && name[i]; ++i)
		prefix = (prefix << CHAR_BIT) | name[i];

	return prefix;
}

/**
 * struct dpu_fence - release/retire fence structure
 * @fence: base fence structure
 * @name: name of each fence- it is fence timeline + commit_count
 * @fence_list: list to associated this fence on timeline/context
 * @fd: fd attached to this fence - debugging purpose.
 */
struct dpu_fence {
	struct dma_fence base;
	struct dpu_fence_context *ctx;
	char name[DPU_FENCE_NAME_SIZE];
	struct list_head	fence_list;
	int fd;
};

static void dpu_fence_destroy(struct kref *kref)
{
}

static inline struct dpu_fence *to_dpu_fence(struct dma_fence *fence)
{
	return container_of(fence, struct dpu_fence, base);
}

static const char *dpu_fence_get_driver_name(struct dma_fence *fence)
{
	struct dpu_fence *f = to_dpu_fence(fence);

	return f->name;
}

static const char *dpu_fence_get_timeline_name(struct dma_fence *fence)
{
	struct dpu_fence *f = to_dpu_fence(fence);

	return f->ctx->name;
}

static bool dpu_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static bool dpu_fence_signaled(struct dma_fence *fence)
{
	struct dpu_fence *f = to_dpu_fence(fence);
	bool status;

	status = (int)(fence->seqno - f->ctx->done_count) <= 0 ? true : false;
	DPU_DEBUG("status:%d fence seq:%d and timeline:%d\n",
			status, fence->seqno, f->ctx->done_count);
	return status;
}

static void dpu_fence_release(struct dma_fence *fence)
{
	struct dpu_fence *f;

	if (fence) {
		f = to_dpu_fence(fence);
		kfree(f);
	}
}

static void dpu_fence_value_str(struct dma_fence *fence, char *str, int size)
{
	if (!fence || !str)
		return;

	snprintf(str, size, "%d", fence->seqno);
}

static void dpu_fence_timeline_value_str(struct dma_fence *fence, char *str,
		int size)
{
	struct dpu_fence *f = to_dpu_fence(fence);

	if (!fence || !f->ctx || !str)
		return;

	snprintf(str, size, "%d", f->ctx->done_count);
}

static struct dma_fence_ops dpu_fence_ops = {
	.get_driver_name = dpu_fence_get_driver_name,
	.get_timeline_name = dpu_fence_get_timeline_name,
	.enable_signaling = dpu_fence_enable_signaling,
	.signaled = dpu_fence_signaled,
	.wait = dma_fence_default_wait,
	.release = dpu_fence_release,
	.fence_value_str = dpu_fence_value_str,
	.timeline_value_str = dpu_fence_timeline_value_str,
};

/**
 * _dpu_fence_create_fd - create fence object and return an fd for it
 * This function is NOT thread-safe.
 * @timeline: Timeline to associate with fence
 * @val: Timeline value at which to signal the fence
 * Return: File descriptor on success, or error code on error
 */
static int _dpu_fence_create_fd(void *fence_ctx, uint32_t val)
{
	struct dpu_fence *dpu_fence;
	struct sync_file *sync_file;
	signed int fd = -EINVAL;
	struct dpu_fence_context *ctx = fence_ctx;

	if (!ctx) {
		DPU_ERROR("invalid context\n");
		goto exit;
	}

	dpu_fence = kzalloc(sizeof(*dpu_fence), GFP_KERNEL);
	if (!dpu_fence)
		return -ENOMEM;

	dpu_fence->ctx = fence_ctx;
	snprintf(dpu_fence->name, DPU_FENCE_NAME_SIZE, "dpu_fence:%s:%u",
						dpu_fence->ctx->name, val);
	dma_fence_init(&dpu_fence->base, &dpu_fence_ops, &ctx->lock,
		ctx->context, val);

	/* create fd */
	fd = get_unused_fd_flags(0);
	if (fd < 0) {
		dma_fence_put(&dpu_fence->base);
		DPU_ERROR("failed to get_unused_fd_flags(), %s\n",
							dpu_fence->name);
		goto exit;
	}

	/* create fence */
	sync_file = sync_file_create(&dpu_fence->base);
	if (sync_file == NULL) {
		put_unused_fd(fd);
		fd = -EINVAL;
		dma_fence_put(&dpu_fence->base);
		DPU_ERROR("couldn't create fence, %s\n", dpu_fence->name);
		goto exit;
	}

	fd_install(fd, sync_file->file);
	dpu_fence->fd = fd;
	kref_get(&ctx->kref);

	spin_lock(&ctx->list_lock);
	list_add_tail(&dpu_fence->fence_list, &ctx->fence_list_head);
	spin_unlock(&ctx->list_lock);

exit:
	return fd;
}

int dpu_fence_init(struct dpu_fence_context *ctx,
		const char *name, uint32_t drm_id)
{
	if (!ctx || !name) {
		DPU_ERROR("invalid argument(s)\n");
		return -EINVAL;
	}
	memset(ctx, 0, sizeof(*ctx));

	strlcpy(ctx->name, name, ARRAY_SIZE(ctx->name));
	ctx->drm_id = drm_id;
	kref_init(&ctx->kref);
	ctx->context = dma_fence_context_alloc(1);

	spin_lock_init(&ctx->lock);
	spin_lock_init(&ctx->list_lock);
	INIT_LIST_HEAD(&ctx->fence_list_head);

	return 0;
}

void dpu_fence_deinit(struct dpu_fence_context *ctx)
{
	if (!ctx) {
		DPU_ERROR("invalid fence\n");
		return;
	}

	kref_put(&ctx->kref, dpu_fence_destroy);
}

void dpu_fence_prepare(struct dpu_fence_context *ctx)
{
	unsigned long flags;

	if (!ctx) {
		DPU_ERROR("invalid argument(s), fence %pK\n", ctx);
	} else {
		spin_lock_irqsave(&ctx->lock, flags);
		++ctx->commit_count;
		spin_unlock_irqrestore(&ctx->lock, flags);
	}
}

static void _dpu_fence_trigger(struct dpu_fence_context *ctx, ktime_t ts)
{
	unsigned long flags;
	struct dpu_fence *fc, *next;
	bool is_signaled = false;
	struct list_head local_list_head;

	INIT_LIST_HEAD(&local_list_head);

	spin_lock(&ctx->list_lock);
	if (list_empty(&ctx->fence_list_head)) {
		DPU_DEBUG("nothing to trigger!\n");
		spin_unlock(&ctx->list_lock);
		return;
	}

	list_for_each_entry_safe(fc, next, &ctx->fence_list_head, fence_list)
		list_move(&fc->fence_list, &local_list_head);
	spin_unlock(&ctx->list_lock);

	list_for_each_entry_safe(fc, next, &local_list_head, fence_list) {
		spin_lock_irqsave(&ctx->lock, flags);
		fc->base.timestamp = ts;
		is_signaled = dma_fence_is_signaled_locked(&fc->base);
		spin_unlock_irqrestore(&ctx->lock, flags);

		if (is_signaled) {
			list_del_init(&fc->fence_list);
			dma_fence_put(&fc->base);
			kref_put(&ctx->kref, dpu_fence_destroy);
		} else {
			spin_lock(&ctx->list_lock);
			list_move(&fc->fence_list, &ctx->fence_list_head);
			spin_unlock(&ctx->list_lock);
		}
	}
}

int dpu_fence_create(struct dpu_fence_context *ctx, uint64_t *val,
							uint32_t offset)
{
	uint32_t trigger_value;
	int fd, rc = -EINVAL;
	unsigned long flags;

	if (!ctx || !val) {
		DPU_ERROR("invalid argument(s), fence %d, pval %d\n",
				ctx != NULL, val != NULL);
		return rc;
	}

	/*
	 * Allow created fences to have a constant offset with respect
	 * to the timeline. This allows us to delay the fence signalling
	 * w.r.t. the commit completion (e.g., an offset of +1 would
	 * cause fences returned during a particular commit to signal
	 * after an additional delay of one commit, rather than at the
	 * end of the current one.
	 */
	spin_lock_irqsave(&ctx->lock, flags);
	trigger_value = ctx->commit_count + offset;

	spin_unlock_irqrestore(&ctx->lock, flags);

	fd = _dpu_fence_create_fd(ctx, trigger_value);
	*val = fd;
	DPU_DEBUG("fence_create::fd:%d trigger:%d commit:%d offset:%d\n",
				fd, trigger_value, ctx->commit_count, offset);

	DPU_EVT32(ctx->drm_id, trigger_value, fd);

	if (fd >= 0) {
		rc = 0;
		_dpu_fence_trigger(ctx, ktime_get());
	} else {
		rc = fd;
	}

	return rc;
}

void dpu_fence_signal(struct dpu_fence_context *ctx, ktime_t ts,
							bool reset_timeline)
{
	unsigned long flags;

	if (!ctx) {
		DPU_ERROR("invalid ctx, %pK\n", ctx);
		return;
	}

	spin_lock_irqsave(&ctx->lock, flags);
	if (reset_timeline) {
		if ((int)(ctx->done_count - ctx->commit_count) < 0) {
			DPU_ERROR(
				"timeline reset attempt! done count:%d commit:%d\n",
				ctx->done_count, ctx->commit_count);
			ctx->done_count = ctx->commit_count;
			DPU_EVT32(ctx->drm_id, ctx->done_count,
				ctx->commit_count, ktime_to_us(ts),
				reset_timeline, DPU_EVTLOG_FATAL);
		} else {
			spin_unlock_irqrestore(&ctx->lock, flags);
			return;
		}
	} else if ((int)(ctx->done_count - ctx->commit_count) < 0) {
		++ctx->done_count;
		DPU_DEBUG("fence_signal:done count:%d commit count:%d\n",
					ctx->done_count, ctx->commit_count);
	} else {
		DPU_ERROR("extra signal attempt! done count:%d commit:%d\n",
					ctx->done_count, ctx->commit_count);
		DPU_EVT32(ctx->drm_id, ctx->done_count, ctx->commit_count,
			ktime_to_us(ts), reset_timeline, DPU_EVTLOG_FATAL);
		spin_unlock_irqrestore(&ctx->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&ctx->lock, flags);

	DPU_EVT32(ctx->drm_id, ctx->done_count, ctx->commit_count,
			ktime_to_us(ts));

	_dpu_fence_trigger(ctx, ts);
}
