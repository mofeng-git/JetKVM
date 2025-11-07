#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <rk_type.h>
#include <rk_mpi.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <mpp_meta.h>

#include "enc_mpp2.h"
#include "ctrl.h"
#include "log.h"

// Minimal state for copy-based NV12 -> H.264 encoding
static MppCtx     g_ctx = NULL;
static MppApi    *g_api = NULL;
static MppEncCfg  g_cfg = NULL;
static MppBufferGroup g_grp = NULL;
static MppBuffer  g_frm_buf = NULL;  // input NV12 buffer
static MppBuffer  g_pkt_buf = NULL;  // output packet buffer
static MppFrame   g_frame = NULL;

static int g_w = 0, g_h = 0, g_stride_w = 0, g_stride_h = 0;
static int g_fps = 0, g_gop = 0, g_bps = 0;

static inline uint64_t now_us() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

int enc_mpp2_init(int width, int height, int fps, int bitrate_kbps, int gop)
{
    g_w = width; g_h = height; g_fps = fps; g_gop = gop > 0 ? gop : 60; g_bps = bitrate_kbps * 1000;
    g_stride_w = (width  + 15) & ~15; // 16 aligned
    g_stride_h = (height + 15) & ~15;

    if (mpp_create(&g_ctx, &g_api) != MPP_OK) {
        log_error("enc_mpp2: mpp_create failed");
        return -1;
    }
    if (mpp_init(g_ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC) != MPP_OK) {
        log_error("enc_mpp2: mpp_init ENC AVC failed");
        return -1;
    }

    if (mpp_enc_cfg_init(&g_cfg) != MPP_OK) {
        log_error("enc_mpp2: mpp_enc_cfg_init failed");
        return -1;
    }
    if (g_api->control(g_ctx, MPP_ENC_GET_CFG, g_cfg) != MPP_OK) {
        log_error("enc_mpp2: MPP_ENC_GET_CFG failed");
        return -1;
    }

    // Prep
    if (mpp_enc_cfg_set_s32(g_cfg, "prep:width", g_w) ||
        mpp_enc_cfg_set_s32(g_cfg, "prep:height", g_h) ||
        mpp_enc_cfg_set_s32(g_cfg, "prep:hor_stride", g_stride_w) ||
        mpp_enc_cfg_set_s32(g_cfg, "prep:ver_stride", g_stride_h) ||
        mpp_enc_cfg_set_s32(g_cfg, "prep:format", MPP_FMT_YUV420SP)) {
        log_error("enc_mpp2: set prep cfg failed");
        return -1;
    }
    // RC CBR low-latency-ish
    if (mpp_enc_cfg_set_u32(g_cfg, "rc:mode", MPP_ENC_RC_MODE_CBR) ||
        mpp_enc_cfg_set_s32(g_cfg, "rc:bps_target", g_bps) ||
        mpp_enc_cfg_set_s32(g_cfg, "rc:bps_max", g_bps * 105 / 100) ||
        mpp_enc_cfg_set_s32(g_cfg, "rc:bps_min", g_bps *  95 / 100) ||
        mpp_enc_cfg_set_s32(g_cfg, "rc:fps_in_num", g_fps) ||
        mpp_enc_cfg_set_s32(g_cfg, "rc:fps_in_denom", 1) ||
        mpp_enc_cfg_set_s32(g_cfg, "rc:fps_out_num", g_fps) ||
        mpp_enc_cfg_set_s32(g_cfg, "rc:fps_out_denom", 1) ||
        mpp_enc_cfg_set_s32(g_cfg, "rc:gop", g_gop)) {
        log_error("enc_mpp2: set rc cfg failed");
        return -1;
    }
    // H.264 baseline, CABAC off for latency; header on each IDR
    if (mpp_enc_cfg_set_s32(g_cfg, "h264:profile", 66) ||
        mpp_enc_cfg_set_s32(g_cfg, "h264:level", 31) ||
        mpp_enc_cfg_set_s32(g_cfg, "h264:cabac_en", 0)) {
        log_error("enc_mpp2: set h264 cfg failed");
        return -1;
    }
    if (g_api->control(g_ctx, MPP_ENC_SET_CFG, g_cfg) != MPP_OK) {
        log_error("enc_mpp2: MPP_ENC_SET_CFG failed");
        return -1;
    }
    MppEncHeaderMode hdr = MPP_ENC_HEADER_MODE_EACH_IDR;
    (void)g_api->control(g_ctx, MPP_ENC_SET_HEADER_MODE, &hdr);

    // Buffers (internal group for simplicity)
    if (mpp_buffer_group_get_internal(&g_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        log_error("enc_mpp2: buffer_group_get_internal failed");
        return -1;
    }
    size_t frame_size = (size_t)g_stride_w * g_stride_h * 3 / 2;
    if (mpp_buffer_get(g_grp, &g_frm_buf, frame_size) != MPP_OK) {
        log_error("enc_mpp2: alloc frame buffer failed");
        return -1;
    }
    if (mpp_buffer_get(g_grp, &g_pkt_buf, frame_size) != MPP_OK) {
        log_error("enc_mpp2: alloc packet buffer failed");
        return -1;
    }
    if (mpp_frame_init(&g_frame) != MPP_OK) {
        log_error("enc_mpp2: mpp_frame_init failed");
        return -1;
    }
    mpp_frame_set_width(g_frame, g_w);
    mpp_frame_set_height(g_frame, g_h);
    mpp_frame_set_hor_stride(g_frame, g_stride_w);
    mpp_frame_set_ver_stride(g_frame, g_stride_h);
    mpp_frame_set_fmt(g_frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(g_frame, g_frm_buf);

    log_info("enc_mpp2: init %dx%d@%dfps %dkbps gop=%d", g_w, g_h, g_fps, bitrate_kbps, g_gop);
    return 0;
}

int enc_mpp2_encode_nv12(const uint8_t *nv12, size_t bytes, uint64_t pts_usec)
{
    if (!g_ctx || !g_api || !g_frm_buf || !g_pkt_buf) return -1;
    size_t need = (size_t)g_stride_w * g_stride_h * 3 / 2;
    if (bytes < need) {
        // allow smaller if no stride padding in input; copy row by row
        // simple safe path: if bytes != g_w*g_h*3/2 then fallback to padded copy
    }

    // Copy into frame buffer (assuming tightly packed w,h then pad if any)
    uint8_t *dst = (uint8_t*)mpp_buffer_get_ptr(g_frm_buf);
    if (!dst) return -1;
    // Y plane
    for (int y = 0; y < g_h; ++y) {
        memcpy(dst + y * g_stride_w, nv12 + y * g_w, (size_t)g_w);
    }
    // UV plane
    uint8_t *dst_uv = dst + g_stride_w * g_stride_h;
    const uint8_t *src_uv = nv12 + g_w * g_h;
    for (int y = 0; y < g_h/2; ++y) {
        memcpy(dst_uv + y * g_stride_w, src_uv + y * g_w, (size_t)g_w);
    }

    mpp_frame_set_eos(g_frame, 0);
    mpp_frame_set_pts(g_frame, (RK_S64)pts_usec);

    // Prepare packet via meta (as in mpi_enc_test advanced)
    MppPacket packet = NULL;
    if (mpp_packet_init_with_buffer(&packet, g_pkt_buf) != MPP_OK) return -1;
    mpp_packet_set_length(packet, 0);
    MppMeta meta = mpp_frame_get_meta(g_frame);
    if (meta) {
        mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet);
    }

    if (g_api->encode_put_frame(g_ctx, g_frame) != MPP_OK) {
        mpp_packet_deinit(&packet);
        log_warn("enc_mpp2: encode_put_frame failed");
        return -1;
    }

    // Try fetch one packet
    MppPacket out = NULL;
    MPP_RET r = g_api->encode_get_packet(g_ctx, &out);
    if (r == MPP_ERR_TIMEOUT) {
        // no output yet; not fatal in pipeline mode
        mpp_packet_deinit(&packet);
        return 0;
    } else if (r != MPP_OK) {
        mpp_packet_deinit(&packet);
        log_warn("enc_mpp2: encode_get_packet failed %d", r);
        return -1;
    }
    if (out) {
        void *p = mpp_packet_get_data(out);
        size_t len = mpp_packet_get_length(out);
        if (p && len) {
            // copy to a heap buf for Go layer ownership
            uint8_t *buf = (uint8_t*)malloc(len);
            if (buf) {
                memcpy(buf, p, len);
                video_send_frame(buf, (ssize_t)len);
                free(buf);
            }
        }
        mpp_packet_deinit(&out);
    }
    mpp_packet_deinit(&packet);
    return 0;
}

void enc_mpp2_shutdown()
{
    if (g_frame) { mpp_frame_deinit(&g_frame); g_frame = NULL; }
    if (g_frm_buf) { mpp_buffer_put(g_frm_buf); g_frm_buf = NULL; }
    if (g_pkt_buf) { mpp_buffer_put(g_pkt_buf); g_pkt_buf = NULL; }
    if (g_grp) { mpp_buffer_group_put(g_grp); g_grp = NULL; }
    if (g_cfg) { mpp_enc_cfg_deinit(g_cfg); g_cfg = NULL; }
    if (g_ctx && g_api) {
        g_api->reset(g_ctx);
    }
    if (g_ctx) { mpp_destroy(g_ctx); g_ctx = NULL; g_api = NULL; }
    log_info("enc_mpp2: shutdown");
}
