#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef JETKVM_HAVE_MPP
#include <rk_type.h>
#include <rk_mpi.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <mpp_meta.h>
#endif

#include "video_encoder.h"
#include "log.h"

#ifdef JETKVM_HAVE_MPP

typedef struct {
    MppCtx ctx;
    MppApi *api;
    MppEncCfg cfg;
    MppBufferGroup buf_grp;
    MppBuffer frm_buf;
    MppBuffer pkt_buf;
    MppFrame frame;
    MppPacket packet;
    int width;
    int height;
    int stride_w;
    int stride_h;
    int fps;
    int initialized;
} mpp_encoder_ctx_t;

static int mpp_encoder_init(const encoder_config_t *config, void **ctx)
{
    if (!config || !ctx) return -1;
    
    mpp_encoder_ctx_t *enc = (mpp_encoder_ctx_t*)calloc(1, sizeof(mpp_encoder_ctx_t));
    if (!enc) return -1;
    
    enc->width = config->width;
    enc->height = config->height;
    enc->fps = config->fps;
    enc->stride_w = (config->width + 15) & ~15;
    enc->stride_h = (config->height + 15) & ~15;
    
    if (mpp_create(&enc->ctx, &enc->api) != MPP_OK) {
        free(enc);
        return -1;
    }
    
    if (mpp_init(enc->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC) != MPP_OK) {
        mpp_destroy(enc->ctx);
        free(enc);
        return -1;
    }
    
    if (mpp_enc_cfg_init(&enc->cfg) != MPP_OK ||
        enc->api->control(enc->ctx, MPP_ENC_GET_CFG, enc->cfg) != MPP_OK) {
        if (enc->cfg) mpp_enc_cfg_deinit(enc->cfg);
        mpp_destroy(enc->ctx);
        free(enc);
        return -1;
    }
    
    if (mpp_enc_cfg_set_s32(enc->cfg, "prep:width", enc->width) ||
        mpp_enc_cfg_set_s32(enc->cfg, "prep:height", enc->height) ||
        mpp_enc_cfg_set_s32(enc->cfg, "prep:hor_stride", enc->stride_w) ||
        mpp_enc_cfg_set_s32(enc->cfg, "prep:ver_stride", enc->stride_h) ||
        mpp_enc_cfg_set_s32(enc->cfg, "prep:format", MPP_FMT_YUV420SP))
        goto cleanup;
    
    int bps = config->bitrate_kbps * 1000;
    int gop = config->keyint > 0 ? config->keyint : 60;
    
    if (mpp_enc_cfg_set_u32(enc->cfg, "rc:mode", MPP_ENC_RC_MODE_CBR) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:bps_target", bps) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:bps_max", bps * 105 / 100) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:bps_min", bps * 95 / 100) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:fps_in_num", enc->fps) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:fps_in_denom", 1) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:fps_out_num", enc->fps) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:fps_out_denom", 1) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:gop", gop))
        goto cleanup;
    
    if (mpp_enc_cfg_set_s32(enc->cfg, "h264:profile", 66) ||
        mpp_enc_cfg_set_s32(enc->cfg, "h264:level", 31) ||
        mpp_enc_cfg_set_s32(enc->cfg, "h264:cabac_en", 0) ||
        mpp_enc_cfg_set_s32(enc->cfg, "h264:max_bframes", 0))
        goto cleanup;
    
    if (enc->api->control(enc->ctx, MPP_ENC_SET_CFG, enc->cfg) != MPP_OK)
        goto cleanup;
    
    MppEncHeaderMode hdr = config->repeat_headers ? 
                          MPP_ENC_HEADER_MODE_EACH_IDR : 
                          MPP_ENC_HEADER_MODE_DEFAULT;
    enc->api->control(enc->ctx, MPP_ENC_SET_HEADER_MODE, &hdr);
    
    if (mpp_buffer_group_get_internal(&enc->buf_grp, 
                                      MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK)
        goto cleanup;
    
    size_t frame_size = (size_t)enc->stride_w * enc->stride_h * 3 / 2;
    
    if (mpp_buffer_get(enc->buf_grp, &enc->frm_buf, frame_size) != MPP_OK ||
        mpp_buffer_get(enc->buf_grp, &enc->pkt_buf, frame_size) != MPP_OK ||
        mpp_frame_init(&enc->frame) != MPP_OK)
        goto cleanup;
    
    mpp_frame_set_width(enc->frame, enc->width);
    mpp_frame_set_height(enc->frame, enc->height);
    mpp_frame_set_hor_stride(enc->frame, enc->stride_w);
    mpp_frame_set_ver_stride(enc->frame, enc->stride_h);
    mpp_frame_set_fmt(enc->frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(enc->frame, enc->frm_buf);
    
    enc->packet = NULL;
    enc->initialized = 1;
    *ctx = enc;
    
    log_info("mpp: encoder initialized %dx%d@%dfps %dkbps gop=%d",
             config->width, config->height, config->fps, config->bitrate_kbps, gop);
    
    return 0;
    
cleanup:
    if (enc->packet) mpp_packet_deinit(&enc->packet);
    if (enc->frame) mpp_frame_deinit(&enc->frame);
    if (enc->pkt_buf) mpp_buffer_put(enc->pkt_buf);
    if (enc->frm_buf) mpp_buffer_put(enc->frm_buf);
    if (enc->buf_grp) mpp_buffer_group_put(enc->buf_grp);
    if (enc->cfg) mpp_enc_cfg_deinit(enc->cfg);
    if (enc->ctx && enc->api) enc->api->reset(enc->ctx);
    if (enc->ctx) mpp_destroy(enc->ctx);
    free(enc);
    return -1;
}

static int mpp_encoder_encode(void *ctx, const video_frame_t *frame, encoded_packet_t *packet)
{
    if (!ctx || !frame || !packet || frame->format != PIXEL_FORMAT_NV12) return -1;
    
    mpp_encoder_ctx_t *enc = (mpp_encoder_ctx_t*)ctx;
    if (!enc->initialized) return -1;
    
    uint8_t *dst = (uint8_t*)mpp_buffer_get_ptr(enc->frm_buf);
    if (!dst) return -1;
    
    if (frame->stride_y == enc->stride_w && frame->stride_uv == enc->stride_w) {
        size_t total_size = (size_t)enc->stride_w * enc->stride_h * 3 / 2;
        if (frame->size >= total_size) {
            memcpy(dst, frame->data, total_size);
            goto encode_ready;
        }
    }
    
    if (frame->stride_y > 0 && frame->stride_y != enc->stride_w) {
        for (int y = 0; y < enc->height; y++) {
            memcpy(dst + y * enc->stride_w, 
                   frame->data + y * frame->stride_y, 
                   enc->width);
        }
    } else {
        for (int y = 0; y < enc->height; y++) {
            memcpy(dst + y * enc->stride_w,
                   frame->data + y * enc->width,
                   enc->width);
        }
    }
    
    uint8_t *dst_uv = dst + enc->stride_w * enc->stride_h;
    const uint8_t *src_uv = frame->data + frame->width * frame->height;
    
    if (frame->stride_uv > 0 && frame->stride_uv != enc->stride_w) {
        for (int y = 0; y < enc->height / 2; y++) {
            memcpy(dst_uv + y * enc->stride_w,
                   src_uv + y * frame->stride_uv,
                   enc->width);
        }
    } else {
        for (int y = 0; y < enc->height / 2; y++) {
            memcpy(dst_uv + y * enc->stride_w,
                   src_uv + y * enc->width,
                   enc->width);
        }
    }

encode_ready:
    
    mpp_frame_set_eos(enc->frame, 0);
    mpp_frame_set_pts(enc->frame, (RK_S64)frame->pts_us);
    
    MppMeta meta = mpp_frame_get_meta(enc->frame);
    
    if (mpp_packet_init_with_buffer(&enc->packet, enc->pkt_buf) != MPP_OK) return -1;
    mpp_packet_set_length(enc->packet, 0);
    
    if (meta) {
        mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, enc->packet);
        mpp_meta_set_buffer(meta, KEY_MOTION_INFO, NULL);
    }
    
    if (enc->api->encode_put_frame(enc->ctx, enc->frame) != MPP_OK) return -1;
    
    MppPacket out_pkt = NULL;
    MPP_RET ret = enc->api->encode_get_packet(enc->ctx, &out_pkt);
    
    if (ret == MPP_ERR_TIMEOUT || !out_pkt) {
        packet->data = NULL;
        packet->size = 0;
        return 0;
    }
    if (ret != MPP_OK) return -1;
    
    void *data = mpp_packet_get_data(out_pkt);
    size_t len = mpp_packet_get_length(out_pkt);
    
    if (data && len > 0) {
        uint8_t *output = (uint8_t*)malloc(len);
        if (!output) return -1;
        
        memcpy(output, data, len);
        
        packet->data = output;
        packet->size = len;
        
        int is_idr = 0;
        if (len >= 5) {
            int nal_start = 0;
            if (output[0] == 0 && output[1] == 0 && output[2] == 0 && output[3] == 1) {
                nal_start = 4;
            } else if (output[0] == 0 && output[1] == 0 && output[2] == 1) {
                nal_start = 3;
            }
            if (nal_start > 0) {
                uint8_t nal_type = output[nal_start] & 0x1F;
                is_idr = (nal_type == 5);
            }
        }
        packet->is_keyframe = is_idr;
        packet->pts_us = (uint64_t)mpp_packet_get_pts(out_pkt);
    } else {
        packet->data = NULL;
        packet->size = 0;
    }
    
    mpp_packet_deinit(&out_pkt);
    enc->packet = NULL;
    
    return 0;
}

static int mpp_encoder_flush(void *ctx, encoded_packet_t *packet)
{
    // MPP编码器的flush可以通过发送EOS frame实现
    // 暂时简化处理：返回0表示无更多帧
    (void)ctx;
    (void)packet;
    return 0;
}

static int mpp_encoder_reconfigure(void *ctx, const encoder_config_t *config)
{
    if (!ctx || !config) {
        return -1;
    }
    
    mpp_encoder_ctx_t *enc = (mpp_encoder_ctx_t*)ctx;
    if (!enc->initialized) {
        return -1;
    }
    
    // MPP支持动态码率调整
    int bps = config->bitrate_kbps * 1000;
    int gop = config->keyint > 0 ? config->keyint : 60;
    
    if (enc->api->control(enc->ctx, MPP_ENC_GET_CFG, enc->cfg) != MPP_OK) {
        return -1;
    }
    
    if (mpp_enc_cfg_set_s32(enc->cfg, "rc:bps_target", bps) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:bps_max", bps * 105 / 100) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:bps_min", bps * 95 / 100) ||
        mpp_enc_cfg_set_s32(enc->cfg, "rc:gop", gop)) {
        log_warn("mpp: reconfigure rc params failed");
        return -1;
    }
    
    if (enc->api->control(enc->ctx, MPP_ENC_SET_CFG, enc->cfg) != MPP_OK) {
        log_warn("mpp: MPP_ENC_SET_CFG failed");
        return -1;
    }
    
    log_info("mpp: reconfigured bitrate=%dkbps gop=%d", config->bitrate_kbps, gop);
    return 0;
}

static pixel_format_t mpp_encoder_get_input_format(void *ctx)
{
    (void)ctx;
    return PIXEL_FORMAT_NV12;
}

static void mpp_encoder_destroy(void **ctx)
{
    if (!ctx || !*ctx) {
        return;
    }
    
    mpp_encoder_ctx_t *enc = (mpp_encoder_ctx_t*)*ctx;
    
    if (enc->initialized) {
        if (enc->packet) mpp_packet_deinit(&enc->packet);
        if (enc->frame) mpp_frame_deinit(&enc->frame);
        if (enc->pkt_buf) mpp_buffer_put(enc->pkt_buf);
        if (enc->frm_buf) mpp_buffer_put(enc->frm_buf);
        if (enc->buf_grp) mpp_buffer_group_put(enc->buf_grp);
        if (enc->cfg) mpp_enc_cfg_deinit(enc->cfg);
        if (enc->ctx && enc->api) enc->api->reset(enc->ctx);
        if (enc->ctx) mpp_destroy(enc->ctx);
        enc->initialized = 0;
    }
    
    free(enc);
    *ctx = NULL;
    
    log_info("mpp: encoder destroyed");
}

// MPP编码器ops实现
static const encoder_ops_t mpp_ops = {
    .init = mpp_encoder_init,
    .encode = mpp_encoder_encode,
    .flush = mpp_encoder_flush,
    .reconfigure = mpp_encoder_reconfigure,
    .get_input_format = mpp_encoder_get_input_format,
    .destroy = mpp_encoder_destroy,
    .name = "mpp"
};

const encoder_ops_t* encoder_get_mpp_ops(void)
{
    return &mpp_ops;
}

#else // !JETKVM_HAVE_MPP

// MPP未启用时的桩实现
const encoder_ops_t* encoder_get_mpp_ops(void)
{
    log_warn("mpp: MPP not enabled at build time");
    return NULL;
}

#endif // JETKVM_HAVE_MPP

