#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <x264.h>
#include "video_encoder.h"
#include "log.h"

// x264编码器上下文
typedef struct {
    x264_t *encoder;
    x264_param_t param;
    x264_picture_t pic_in;
    int width;
    int height;
    int initialized;
} x264_encoder_ctx_t;

static int x264_encoder_init(const encoder_config_t *config, void **ctx)
{
    if (!config || !ctx) {
        log_error("x264: invalid parameters");
        return -1;
    }
    
    x264_encoder_ctx_t *enc = (x264_encoder_ctx_t*)calloc(1, sizeof(x264_encoder_ctx_t));
    if (!enc) {
        log_error("x264: failed to allocate context");
        return -1;
    }
    
    // 设置默认preset和tune
    const char *preset = config->x264_preset[0] ? config->x264_preset : "ultrafast";
    const char *tune = config->x264_tune[0] ? config->x264_tune : "zerolatency";
    const char *profile = config->x264_profile[0] ? config->x264_profile : "baseline";
    
    x264_param_default_preset(&enc->param, preset, tune);
    
    // 基本参数
    enc->param.i_csp = X264_CSP_I420;
    enc->param.i_width = config->width;
    enc->param.i_height = config->height;
    enc->param.i_fps_num = config->fps;
    enc->param.i_fps_den = 1;
    enc->param.i_keyint_max = config->keyint > 0 ? config->keyint : 60;
    enc->param.b_repeat_headers = config->repeat_headers;
    enc->param.b_annexb = 1;  // Annex B格式（用于RTP）
    enc->param.b_vfr_input = 0;
    
    // 码率控制
    enc->param.rc.i_rc_method = X264_RC_ABR;
    enc->param.rc.i_bitrate = config->bitrate_kbps;
    enc->param.i_threads = 0;  // 自动检测
    
    // 应用profile
    if (x264_param_apply_profile(&enc->param, profile) < 0) {
        log_warn("x264: failed to apply profile %s, continuing", profile);
    }
    
    // 打开编码器
    enc->encoder = x264_encoder_open(&enc->param);
    if (!enc->encoder) {
        log_error("x264: x264_encoder_open failed");
        free(enc);
        return -1;
    }
    
    // 分配图像结构
    if (x264_picture_alloc(&enc->pic_in, X264_CSP_I420, config->width, config->height) < 0) {
        log_error("x264: x264_picture_alloc failed");
        x264_encoder_close(enc->encoder);
        free(enc);
        return -1;
    }
    
    enc->width = config->width;
    enc->height = config->height;
    enc->initialized = 1;
    *ctx = enc;
    
    log_info("x264: encoder initialized %dx%d@%dfps %dkbps preset=%s tune=%s profile=%s",
             config->width, config->height, config->fps, config->bitrate_kbps,
             preset, tune, profile);
    
    return 0;
}

static int x264_encoder_do_encode(void *ctx, const video_frame_t *frame, encoded_packet_t *packet)
{
    if (!ctx || !frame || !packet) {
        return -1;
    }
    
    x264_encoder_ctx_t *enc = (x264_encoder_ctx_t*)ctx;
    if (!enc->initialized) {
        log_error("x264: encoder not initialized");
        return -1;
    }
    
    // 检查格式
    if (frame->format != PIXEL_FORMAT_I420) {
        log_error("x264: unsupported pixel format %d, expected I420", frame->format);
        return -1;
    }
    
    // 设置图像平面（假设输入是紧凑的I420）
    if (frame->stride_y > 0) {
        // 使用外部stride
        enc->pic_in.img.plane[0] = frame->data;
        enc->pic_in.img.plane[1] = frame->data + frame->stride_y * frame->height;
        enc->pic_in.img.plane[2] = enc->pic_in.img.plane[1] + frame->stride_uv * (frame->height / 2);
        enc->pic_in.img.i_stride[0] = frame->stride_y;
        enc->pic_in.img.i_stride[1] = frame->stride_uv;
        enc->pic_in.img.i_stride[2] = frame->stride_uv;
    } else {
        // 默认紧凑布局
        enc->pic_in.img.plane[0] = frame->data;
        enc->pic_in.img.plane[1] = frame->data + enc->width * enc->height;
        enc->pic_in.img.plane[2] = enc->pic_in.img.plane[1] + (enc->width / 2) * (enc->height / 2);
        enc->pic_in.img.i_stride[0] = enc->width;
        enc->pic_in.img.i_stride[1] = enc->width / 2;
        enc->pic_in.img.i_stride[2] = enc->width / 2;
    }
    
    enc->pic_in.i_pts = (int64_t)frame->pts_us;
    
    // 编码
    x264_nal_t *nals = NULL;
    int i_nals = 0;
    x264_picture_t pic_out;
    
    int bytes = x264_encoder_encode(enc->encoder, &nals, &i_nals, &enc->pic_in, &pic_out);
    if (bytes < 0) {
        log_warn("x264: x264_encoder_encode failed");
        return -1;
    }
    
    if (bytes == 0) {
        // 没有输出（延迟编码）
        packet->data = NULL;
        packet->size = 0;
        return 0;
    }
    
    // 聚合所有NAL单元
    int total_size = 0;
    for (int i = 0; i < i_nals; i++) {
        total_size += nals[i].i_payload;
    }
    
    uint8_t *output = (uint8_t*)malloc(total_size);
    if (!output) {
        log_error("x264: failed to allocate output buffer");
        return -1;
    }
    
    int offset = 0;
    for (int i = 0; i < i_nals; i++) {
        memcpy(output + offset, nals[i].p_payload, nals[i].i_payload);
        offset += nals[i].i_payload;
    }
    
    // 填充packet
    packet->data = output;
    packet->size = total_size;
    packet->is_keyframe = (pic_out.b_keyframe != 0);
    packet->pts_us = (uint64_t)pic_out.i_pts;
    
    return 0;
}

static int x264_encoder_do_flush(void *ctx, encoded_packet_t *packet)
{
    if (!ctx || !packet) {
        return -1;
    }
    
    x264_encoder_ctx_t *enc = (x264_encoder_ctx_t*)ctx;
    if (!enc->initialized) {
        return -1;
    }
    
    // 使用NULL输入来刷新延迟帧（调用x264库的函数）
    x264_nal_t *nals = NULL;
    int i_nals = 0;
    x264_picture_t pic_out;
    
    // 注意：这里调用的是x264库的x264_encoder_encode函数
    int bytes = x264_encoder_encode(enc->encoder, &nals, &i_nals, NULL, &pic_out);
    if (bytes <= 0) {
        return bytes;  // 0=no more frames, <0=error
    }
    
    // 同encode逻辑
    int total_size = 0;
    for (int i = 0; i < i_nals; i++) {
        total_size += nals[i].i_payload;
    }
    
    uint8_t *output = (uint8_t*)malloc(total_size);
    if (!output) {
        return -1;
    }
    
    int offset = 0;
    for (int i = 0; i < i_nals; i++) {
        memcpy(output + offset, nals[i].p_payload, nals[i].i_payload);
        offset += nals[i].i_payload;
    }
    
    packet->data = output;
    packet->size = total_size;
    packet->is_keyframe = (pic_out.b_keyframe != 0);
    packet->pts_us = (uint64_t)pic_out.i_pts;
    
    return 0;
}

static int x264_encoder_reconfigure(void *ctx, const encoder_config_t *config)
{
    if (!ctx || !config) {
        return -1;
    }
    
    x264_encoder_ctx_t *enc = (x264_encoder_ctx_t*)ctx;
    if (!enc->initialized) {
        return -1;
    }
    
    // x264支持部分参数的动态调整
    x264_param_t param;
    memcpy(&param, &enc->param, sizeof(x264_param_t));
    
    // 可调整的参数
    param.rc.i_bitrate = config->bitrate_kbps;
    param.i_keyint_max = config->keyint > 0 ? config->keyint : 60;
    
    if (x264_encoder_reconfig(enc->encoder, &param) < 0) {
        log_warn("x264: x264_encoder_reconfig failed, may need full restart");
        return -1;
    }
    
    memcpy(&enc->param, &param, sizeof(x264_param_t));
    log_info("x264: reconfigured bitrate=%dkbps keyint=%d", 
             config->bitrate_kbps, config->keyint);
    
    return 0;
}

static pixel_format_t x264_encoder_get_input_format(void *ctx)
{
    (void)ctx;
    return PIXEL_FORMAT_I420;
}

static void x264_encoder_destroy(void **ctx)
{
    if (!ctx || !*ctx) {
        return;
    }
    
    x264_encoder_ctx_t *enc = (x264_encoder_ctx_t*)*ctx;
    
    if (enc->initialized) {
        if (enc->encoder) {
            x264_encoder_close(enc->encoder);
            enc->encoder = NULL;
        }
        x264_picture_clean(&enc->pic_in);
        enc->initialized = 0;
    }
    
    free(enc);
    *ctx = NULL;
    
    log_info("x264: encoder destroyed");
}

// x264编码器ops实现
static const encoder_ops_t x264_ops = {
    .init = x264_encoder_init,
    .encode = x264_encoder_do_encode,
    .flush = x264_encoder_do_flush,
    .reconfigure = x264_encoder_reconfigure,
    .get_input_format = x264_encoder_get_input_format,
    .destroy = x264_encoder_destroy,
    .name = "x264"
};

const encoder_ops_t* encoder_get_x264_ops(void)
{
    return &x264_ops;
}

