/*
 * Copyright (C) 2018 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdp5_kms.h"

/*
 * Writeback connector/encoder implementation:
 */

struct mdp5_wb_connector {
	struct drm_writeback_connector base;

	u32 nformats;
	u32 formats[32];

	unsigned id;
	struct mdp5_ctl *ctl;
	struct mdp5_interface *intf;

	struct mdp_irq wb_done;
};
#define to_mdp5_wb_connector(x) container_of(x, struct mdp5_wb_connector, base)

struct mdp5_wb_connector_state {
	struct drm_connector_state base;
	// XXX maybe we don't need to subclass state.. at least not yet,
	// maybe there are some custom properties we'd want to add?
};
#define to_mdp5_wb_connector_state(x) container_of(x, struct mdp5_wb_connector_state, base)

static int mdp5_wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return drm_add_modes_noedid(connector, dev->mode_config.max_width,
		dev->mode_config.max_height);
}

static enum drm_mode_status
mdp5_wb_connector_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	int w = mode->hdisplay, h = mode->vdisplay;

	if ((w < mode_config->min_width) || (w > mode_config->max_width))
		return MODE_BAD_HVALUE;

	if ((h < mode_config->min_height) || (h > mode_config->max_height))
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

const struct drm_connector_helper_funcs mdp5_wb_connector_helper_funcs = {
	.get_modes = mdp5_wb_connector_get_modes,
	.mode_valid = mdp5_wb_connector_mode_valid,
};

static void mdp5_wb_connector_reset(struct drm_connector *connector)
{
	struct mdp5_wb_connector_state *mdp5_wb_state =
			kzalloc(sizeof(*mdp5_wb_state), GFP_KERNEL);

	if (connector->state)
		__drm_atomic_helper_connector_destroy_state(connector->state);

	kfree(connector->state);
	__drm_atomic_helper_connector_reset(connector, &mdp5_wb_state->base);
}

static enum drm_connector_status
mdp5_wb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_disconnected;
}

static void mdp5_wb_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static struct drm_connector_state *
mdp5_wb_connector_duplicate_state(struct drm_connector *connector)
{
	struct mdp5_wb_connector_state *mdp5_wb_state;

	if (WARN_ON(!connector->state))
		return NULL;

	mdp5_wb_state = kzalloc(sizeof(*mdp5_wb_state), GFP_KERNEL);
	if (!mdp5_wb_state)
		return NULL;

	/* No need to preserve any of our driver-local data */
	__drm_atomic_helper_connector_duplicate_state(connector, &mdp5_wb_state->base);

	return &mdp5_wb_state->base;
}

static void
mdp5_wb_connector_connector_destroy_state(struct drm_connector *connector,
		struct drm_connector_state *state)
{
	struct mdp5_wb_connector_state *mdp5_wb_state =
		to_mdp5_wb_connector_state(state);

	__drm_atomic_helper_connector_destroy_state(state);
	kfree(mdp5_wb_state);
}

static const struct drm_connector_funcs mdp5_wb_connector_funcs = {
	.reset = mdp5_wb_connector_reset,
	.detect = mdp5_wb_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = mdp5_wb_connector_destroy,
	.atomic_duplicate_state = mdp5_wb_connector_duplicate_state,
	.atomic_destroy_state = mdp5_wb_connector_connector_destroy_state,
};

static int
mdp5_wb_encoder_atomic_check(struct drm_encoder *encoder,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc_state);
	struct mdp5_wb_connector *mdp5_wb = to_mdp5_wb_connector(
		to_wb_connector(conn_state->connector));
	struct drm_framebuffer *fb;
	const struct msm_format *format;
	const struct mdp_format *mdp_fmt;
	struct drm_format_name_buf format_name;
	int ret;

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	fb = conn_state->writeback_job->fb;

	DBG("wb[%u]: check writeback %ux%u@%s", mdp5_wb->id,
		fb->width, fb->height,
		drm_get_format_name(fb->format->format, &format_name));

	format = mdp_get_format(priv->kms, fb->format->format);
	if (!format) {
		DBG("Invalid pixel format!");
		return -EINVAL;
	}

	mdp_fmt = to_mdp_format(format);
	if (MDP_FORMAT_IS_YUV(mdp_fmt)) {
		switch (mdp_fmt->chroma_sample) {
		case CHROMA_420:
		case CHROMA_H2V1:
			/* supported */
			break;
		case CHROMA_H1V2:
		default:
			DBG("unsupported wb chroma samp=%d\n",
				mdp_fmt->chroma_sample);
			return -EINVAL;
		}
	}

	/* TODO I think we would prefer to have proper prepare_fb()/cleanup_fb()
	 * vfuncs, as with plane..  Also, where to unprepare?
	 */
	ret = msm_framebuffer_prepare(fb, priv->kms->aspace);
	if (ret)
		return ret;

	mdp5_cstate->ctl = mdp5_wb->ctl;
	mdp5_cstate->pipeline.intf = mdp5_wb->intf;
	mdp5_cstate->defer_start = true;

	return 0;
}

static void
wb_csc_setup(struct mdp5_kms *mdp5_kms, u32 wb_id, struct csc_cfg *csc)
{
	uint32_t  i;
	uint32_t *matrix;

	if (unlikely(!csc))
		return;

	matrix = csc->matrix;
	mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_MATRIX_COEFF_0(wb_id),
		MDP5_WB_CSC_MATRIX_COEFF_0_COEFF_11(matrix[0]) |
		MDP5_WB_CSC_MATRIX_COEFF_0_COEFF_12(matrix[1]));
	mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_MATRIX_COEFF_1(wb_id),
		MDP5_WB_CSC_MATRIX_COEFF_1_COEFF_13(matrix[2]) |
		MDP5_WB_CSC_MATRIX_COEFF_1_COEFF_21(matrix[3]));
	mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_MATRIX_COEFF_2(wb_id),
		MDP5_WB_CSC_MATRIX_COEFF_2_COEFF_22(matrix[4]) |
		MDP5_WB_CSC_MATRIX_COEFF_2_COEFF_23(matrix[5]));
	mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_MATRIX_COEFF_3(wb_id),
		MDP5_WB_CSC_MATRIX_COEFF_3_COEFF_31(matrix[6]) |
		MDP5_WB_CSC_MATRIX_COEFF_3_COEFF_32(matrix[7]));
	mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_MATRIX_COEFF_4(wb_id),
		MDP5_WB_CSC_MATRIX_COEFF_4_COEFF_33(matrix[8]));

	for (i = 0; i < ARRAY_SIZE(csc->pre_bias); i++) {
		uint32_t *pre_clamp = csc->pre_clamp;
		uint32_t *post_clamp = csc->post_clamp;

		mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_COMP_PRECLAMP(wb_id, i),
			MDP5_WB_CSC_COMP_PRECLAMP_REG_HIGH(pre_clamp[2*i+1]) |
			MDP5_WB_CSC_COMP_PRECLAMP_REG_LOW(pre_clamp[2*i]));

		mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_COMP_POSTCLAMP(wb_id, i),
			MDP5_WB_CSC_COMP_POSTCLAMP_REG_HIGH(post_clamp[2*i+1]) |
			MDP5_WB_CSC_COMP_POSTCLAMP_REG_LOW(post_clamp[2*i]));

		mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_COMP_PREBIAS(wb_id, i),
			MDP5_WB_CSC_COMP_PREBIAS_REG_VALUE(csc->pre_bias[i]));

		mdp5_write(mdp5_kms, REG_MDP5_WB_CSC_COMP_POSTBIAS(wb_id, i),
			MDP5_WB_CSC_COMP_POSTBIAS_REG_VALUE(csc->post_bias[i]));
	}
}

void
mdp5_wb_atomic_commit(struct drm_connector *connector)
{
	struct msm_drm_private *priv = connector->dev->dev_private;
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(priv->kms));
	struct drm_connector_state *conn_state = connector->state;
	struct drm_writeback_connector *wb_conn = to_wb_connector(connector);
	struct mdp5_crtc_state *mdp5_crtc_state =
		to_mdp5_crtc_state(wb_conn->encoder.crtc->state);
	struct mdp5_wb_connector *mdp5_wb = to_mdp5_wb_connector(wb_conn);
	struct drm_writeback_job *job = conn_state->writeback_job;
	struct drm_framebuffer *fb = job->fb;
	struct drm_format_name_buf format_name;
	const struct mdp_format *fmt =
		to_mdp_format(mdp_get_format(priv->kms, fb->format->format));
	u32 ystride0, ystride1, outsize;
	u32 dst_format, pattern, opmode = 0;

	DBG("wb[%u]: kick writeback %ux%u@%s", mdp5_wb->id,
		fb->width, fb->height,
		drm_get_format_name(fb->format->format, &format_name));

	/* queue job before anything that can trigger completion irq */
	drm_writeback_queue_job(wb_conn, job);
	conn_state->writeback_job = NULL;

	mdp_irq_register(&mdp5_kms->base, &mdp5_wb->wb_done);

	if (MDP_FORMAT_IS_YUV(fmt)) {
		wb_csc_setup(mdp5_kms, mdp5_wb->id,
			mdp_get_default_csc_cfg(CSC_RGB2YUV));

		opmode |= MDP5_WB_DST_OP_MODE_CSC_EN |
			MDP5_WB_DST_OP_MODE_CSC_SRC_DATA_FORMAT(DATA_FORMAT_RGB) |
			MDP5_WB_DST_OP_MODE_CSC_DST_DATA_FORMAT(DATA_FORMAT_YUV);

		switch (fmt->chroma_sample) {
		case CHROMA_420:
		case CHROMA_H2V1:
			opmode |= MDP5_WB_DST_OP_MODE_CHROMA_DWN_SAMPLE_EN;
			break;
		case CHROMA_H1V2:
		default:
			WARN(1, "unsupported wb chroma samp=%d\n",
				fmt->chroma_sample);
			return;
		}
	}

	dst_format = MDP5_WB_DST_FORMAT_DST_CHROMA_SAMP(fmt->chroma_sample) |
		MDP5_WB_DST_FORMAT_WRITE_PLANES(fmt->fetch_type) |
		MDP5_WB_DST_FORMAT_DSTC3_OUT(fmt->bpc_a) |
		MDP5_WB_DST_FORMAT_DSTC2_OUT(fmt->bpc_r) |
		MDP5_WB_DST_FORMAT_DSTC1_OUT(fmt->bpc_b) |
		MDP5_WB_DST_FORMAT_DSTC0_OUT(fmt->bpc_g) |
		COND(fmt->unpack_tight, MDP5_WB_DST_FORMAT_PACK_TIGHT) |
		MDP5_WB_DST_FORMAT_PACK_COUNT(fmt->unpack_count - 1) |
		MDP5_WB_DST_FORMAT_DST_BPP(fmt->cpp - 1);

	if (fmt->bpc_a || fmt->alpha_enable) {
		dst_format |= MDP5_WB_DST_FORMAT_DSTC3_EN;
		if (!fmt->alpha_enable)
			dst_format |= MDP5_WB_DST_FORMAT_DST_ALPHA_X;
	}

	pattern = MDP5_WB_DST_PACK_PATTERN_ELEMENT3(fmt->unpack[3]) |
		MDP5_WB_DST_PACK_PATTERN_ELEMENT2(fmt->unpack[2]) |
		MDP5_WB_DST_PACK_PATTERN_ELEMENT1(fmt->unpack[1]) |
		MDP5_WB_DST_PACK_PATTERN_ELEMENT0(fmt->unpack[0]);

	ystride0 = MDP5_WB_DST_YSTRIDE0_DST0_YSTRIDE(fb->pitches[0]) |
		MDP5_WB_DST_YSTRIDE0_DST1_YSTRIDE(fb->pitches[1]);
	ystride1 = MDP5_WB_DST_YSTRIDE1_DST2_YSTRIDE(fb->pitches[2]) |
		MDP5_WB_DST_YSTRIDE1_DST3_YSTRIDE(fb->pitches[3]);

	/* get the output resolution from WB device */
	outsize = MDP5_WB_OUT_SIZE_DST_H(fb->height) |
		MDP5_WB_OUT_SIZE_DST_W(fb->width);

	mdp5_write(mdp5_kms, REG_MDP5_WB_ALPHA_X_VALUE(mdp5_wb->id), 0xff);
	mdp5_write(mdp5_kms, REG_MDP5_WB_DST_FORMAT(mdp5_wb->id), dst_format);
	mdp5_write(mdp5_kms, REG_MDP5_WB_DST_OP_MODE(mdp5_wb->id), opmode);
	mdp5_write(mdp5_kms, REG_MDP5_WB_DST_PACK_PATTERN(mdp5_wb->id), pattern);
	mdp5_write(mdp5_kms, REG_MDP5_WB_DST_YSTRIDE0(mdp5_wb->id), ystride0);
	mdp5_write(mdp5_kms, REG_MDP5_WB_DST_YSTRIDE1(mdp5_wb->id), ystride1);
	mdp5_write(mdp5_kms, REG_MDP5_WB_OUT_SIZE(mdp5_wb->id), outsize);

	mdp5_crtc_set_pipeline(wb_conn->encoder.crtc);

	mdp5_write(mdp5_kms, REG_MDP5_WB_DST0_ADDR(mdp5_wb->id),
		msm_framebuffer_iova(fb, priv->kms->aspace, 0));
	mdp5_write(mdp5_kms, REG_MDP5_WB_DST1_ADDR(mdp5_wb->id),
		msm_framebuffer_iova(fb, priv->kms->aspace, 1));
	mdp5_write(mdp5_kms, REG_MDP5_WB_DST2_ADDR(mdp5_wb->id),
		msm_framebuffer_iova(fb, priv->kms->aspace, 2));
	mdp5_write(mdp5_kms, REG_MDP5_WB_DST3_ADDR(mdp5_wb->id),
		msm_framebuffer_iova(fb, priv->kms->aspace, 3));

	/* Notify ctl that wb buffer is ready to trigger start */
	mdp5_ctl_commit(mdp5_wb->ctl, &mdp5_crtc_state->pipeline,
		MDP5_CTL_FLUSH_WB, true);

	mdp5_ctl_set_encoder_state(mdp5_wb->ctl,
		&mdp5_crtc_state->pipeline, true);
}

static void mdp5_wb_done_irq(struct mdp_irq *irq, uint32_t irqstatus)
{
	struct mdp5_wb_connector *mdp5_wb =
		container_of(irq, struct mdp5_wb_connector, wb_done);
	struct mdp5_crtc_state *mdp5_crtc_state =
		to_mdp5_crtc_state(mdp5_wb->base.encoder.crtc->state);
	struct msm_drm_private *priv = mdp5_wb->base.base.dev->dev_private;

	mdp_irq_unregister(to_mdp_kms(priv->kms), &mdp5_wb->wb_done);

	mdp5_ctl_set_encoder_state(mdp5_wb->ctl,
		&mdp5_crtc_state->pipeline, false);

	drm_writeback_signal_completion(&mdp5_wb->base, 0);
}

static const struct drm_encoder_helper_funcs mdp5_wb_encoder_helper_funcs = {
	.atomic_check = mdp5_wb_encoder_atomic_check,
};

struct drm_writeback_connector *
mdp5_wb_connector_init(struct drm_device *dev, struct mdp5_ctl *ctl,
		unsigned wb_id)
{
	struct drm_connector *connector = NULL;
	struct mdp5_wb_connector *mdp5_wb;

	mdp5_wb = kzalloc(sizeof(*mdp5_wb), GFP_KERNEL);
	if (!mdp5_wb)
		return ERR_PTR(-ENOMEM);

	mdp5_wb->id = wb_id;
	mdp5_wb->ctl = ctl;

	/* construct a dummy intf for WB: */
// TODO un-inline this (and also in interface_init())
	mdp5_wb->intf = kzalloc(sizeof(*mdp5_wb->intf), GFP_KERNEL);
	mdp5_wb->intf->num = -1;
	mdp5_wb->intf->type = INTF_WB;
	mdp5_wb->intf->mode = MDP5_INTF_WB_MODE_LINE;
	mdp5_wb->intf->idx = -1;

	mdp5_wb->wb_done.irq = mdp5_wb_done_irq;
// TODO just register for all wb irq's until I figure out the mapping..
	mdp5_wb->wb_done.irqmask = MDP5_IRQ_WB_0_DONE | MDP5_IRQ_WB_1_DONE | MDP5_IRQ_WB_2_DONE;

	connector = &mdp5_wb->base.base;

	drm_connector_helper_add(connector, &mdp5_wb_connector_helper_funcs);

	mdp5_wb->nformats = mdp_get_formats(mdp5_wb->formats,
		ARRAY_SIZE(mdp5_wb->formats), false);

	drm_writeback_connector_init(dev,
		&mdp5_wb->base,
		&mdp5_wb_connector_funcs,
		&mdp5_wb_encoder_helper_funcs,
		mdp5_wb->formats,
		mdp5_wb->nformats);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	return &mdp5_wb->base;
}
