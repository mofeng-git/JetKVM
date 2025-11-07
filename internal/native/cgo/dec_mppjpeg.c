#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <rk_mpi.h>
#include <mpp_frame.h>
#include <mpp_packet.h>

#include "dec_mppjpeg.h"
#include "log.h"

static MppCtx  d_ctx = NULL;
static MppApi *d_api = NULL;
static int     inited = 0;
static MppBufferGroup d_pkt_grp = NULL;
static MppBufferGroup d_frm_grp = NULL;
static MppFrame d_frame = NULL;
static MppBuffer d_frm_buf = NULL;
static uint8_t *nv12_buf = NULL;
static size_t nv12_cap = 0;

int dec_mppjpeg_init()
{
    if (inited) return 0;
    
    if (mpp_create(&d_ctx, &d_api) != MPP_OK || 
        mpp_init(d_ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG) != MPP_OK)
        return -1;
    
    RK_S32 timeout = 50;
    d_api->control(d_ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    d_api->control(d_ctx, MPP_SET_INPUT_TIMEOUT, &timeout);
    
    MppDecCfg cfg = NULL;
    if (mpp_dec_cfg_init(&cfg) == MPP_OK && 
        d_api->control(d_ctx, MPP_DEC_GET_CFG, cfg) == MPP_OK) {
        mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1);
        mpp_dec_cfg_set_u32(cfg, "base:fast_parse", 1);
        mpp_dec_cfg_set_u32(cfg, "base:fast_mode", 1);
        d_api->control(d_ctx, MPP_DEC_SET_CFG, cfg);
        mpp_dec_cfg_deinit(cfg);
    }
    
    MppFrameFormat fmt = MPP_FMT_YUV420SP;
    d_api->control(d_ctx, MPP_DEC_SET_OUTPUT_FORMAT, &fmt);
    
    if (mpp_buffer_group_get_internal(&d_pkt_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK &&
        mpp_buffer_group_get_internal(&d_pkt_grp, MPP_BUFFER_TYPE_ION) != MPP_OK &&
        mpp_buffer_group_get_internal(&d_pkt_grp, MPP_BUFFER_TYPE_NORMAL) != MPP_OK)
        return -1;
    
    RK_U32 buf_size = 1936 * 1088 * 4;
    
    if (mpp_buffer_group_get_internal(&d_frm_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK &&
        mpp_buffer_group_get_internal(&d_frm_grp, MPP_BUFFER_TYPE_ION) != MPP_OK &&
        mpp_buffer_group_get_internal(&d_frm_grp, MPP_BUFFER_TYPE_NORMAL) != MPP_OK)
        return -1;
    
    if (mpp_buffer_get(d_frm_grp, &d_frm_buf, buf_size) != MPP_OK ||
        mpp_frame_init(&d_frame) != MPP_OK)
        return -1;
    
    mpp_frame_set_buffer(d_frame, d_frm_buf);
    
    inited = 1;
    return 0;
}

int dec_mppjpeg_decode(const uint8_t *data, size_t len,
                       uint8_t **out_nv12, size_t *out_size,
                       int *out_w, int *out_h)
{
    if (!inited || !data || len < 2 || !out_nv12 || !out_size || !out_w || !out_h ||
        data[0] != 0xFF || data[1] != 0xD8)
        return -1;
    
    MppBuffer inbuf = NULL;
    if (mpp_buffer_get(d_pkt_grp, &inbuf, (RK_U32)len) != MPP_OK) return -1;
    
    void *ptr = mpp_buffer_get_ptr(inbuf);
    memcpy(ptr, data, len);
    
    MppPacket pkt = NULL;
    mpp_packet_init_with_buffer(&pkt, inbuf);
    mpp_buffer_put(inbuf);
    
    mpp_packet_set_data(pkt, ptr);
    mpp_packet_set_pos(pkt, ptr);
    mpp_packet_set_size(pkt, len);
    mpp_packet_set_length(pkt, len);
    
    MppMeta meta = mpp_packet_get_meta(pkt);
    if (meta) mpp_meta_set_frame(meta, KEY_OUTPUT_FRAME, d_frame);
    
    if (d_api->decode_put_packet(d_ctx, pkt) != MPP_OK) {
        mpp_packet_deinit(&pkt);
        return -1;
    }
    
    MppFrame frame_out = NULL;
    if (d_api->decode_get_frame(d_ctx, &frame_out) != MPP_OK || !frame_out ||
        mpp_frame_get_errinfo(frame_out) || mpp_frame_get_discard(frame_out)) {
        mpp_packet_deinit(&pkt);
        return -1;
    }
    
    int width = mpp_frame_get_width(frame_out);
    int height = mpp_frame_get_height(frame_out);
    int hor_stride = mpp_frame_get_hor_stride(frame_out);
    int ver_stride = mpp_frame_get_ver_stride(frame_out);
    
    MppBuffer frm_buf = mpp_frame_get_buffer(frame_out);
    void *frm_ptr = frm_buf ? mpp_buffer_get_ptr(frm_buf) : NULL;
    if (!frm_ptr) {
        mpp_packet_deinit(&pkt);
        return -1;
    }
    
    size_t y_size = (size_t)width * height;
    size_t total_size = y_size + y_size / 2;
    
    if (total_size > nv12_cap) {
        uint8_t *new_buf = (uint8_t*)realloc(nv12_buf, total_size);
        if (!new_buf) {
            mpp_packet_deinit(&pkt);
            return -1;
        }
        nv12_buf = new_buf;
        nv12_cap = total_size;
    }
    
    uint8_t *src_y = (uint8_t*)frm_ptr;
    uint8_t *dst_y = nv12_buf;
    for (int i = 0; i < height; i++)
        memcpy(dst_y + i * width, src_y + i * hor_stride, width);
    
    uint8_t *src_uv = src_y + hor_stride * ver_stride;
    uint8_t *dst_uv = nv12_buf + y_size;
    for (int i = 0; i < height / 2; i++)
        memcpy(dst_uv + i * width, src_uv + i * hor_stride, width);
    
    *out_nv12 = nv12_buf;
    *out_size = total_size;
    *out_w = width;
    *out_h = height;
    
    mpp_packet_deinit(&pkt);
    return 0;
}

int dec_mppjpeg_decode_zero_copy(const uint8_t *data, size_t len,
                                  dec_mppjpeg_frame_t *out_frame)
{
    if (!inited || !data || len < 2 || !out_frame ||
        data[0] != 0xFF || data[1] != 0xD8)
        return -1;
    
    MppBuffer inbuf = NULL;
    if (mpp_buffer_get(d_pkt_grp, &inbuf, (RK_U32)len) != MPP_OK) return -1;
    
    void *ptr = mpp_buffer_get_ptr(inbuf);
    memcpy(ptr, data, len);
    
    MppPacket pkt = NULL;
    mpp_packet_init_with_buffer(&pkt, inbuf);
    mpp_buffer_put(inbuf);
    
    mpp_packet_set_data(pkt, ptr);
    mpp_packet_set_pos(pkt, ptr);
    mpp_packet_set_size(pkt, len);
    mpp_packet_set_length(pkt, len);
    
    MppMeta meta = mpp_packet_get_meta(pkt);
    if (meta) mpp_meta_set_frame(meta, KEY_OUTPUT_FRAME, d_frame);
    
    if (d_api->decode_put_packet(d_ctx, pkt) != MPP_OK) {
        mpp_packet_deinit(&pkt);
        return -1;
    }
    
    MppFrame frame_out = NULL;
    if (d_api->decode_get_frame(d_ctx, &frame_out) != MPP_OK || !frame_out ||
        mpp_frame_get_errinfo(frame_out) || mpp_frame_get_discard(frame_out)) {
        mpp_packet_deinit(&pkt);
        return -1;
    }
    
    MppBuffer frm_buf = mpp_frame_get_buffer(frame_out);
    void *frm_ptr = frm_buf ? mpp_buffer_get_ptr(frm_buf) : NULL;
    if (!frm_ptr) {
        mpp_packet_deinit(&pkt);
        return -1;
    }
    
    out_frame->mpp_frame = (void*)frame_out;
    out_frame->mpp_buffer = (void*)frm_buf;
    out_frame->data_ptr = (uint8_t*)frm_ptr;
    out_frame->width = mpp_frame_get_width(frame_out);
    out_frame->height = mpp_frame_get_height(frame_out);
    out_frame->hor_stride = mpp_frame_get_hor_stride(frame_out);
    out_frame->ver_stride = mpp_frame_get_ver_stride(frame_out);
    
    mpp_packet_deinit(&pkt);
    return 0;
}

void dec_mppjpeg_deinit()
{
    if (d_frame) mpp_frame_deinit(&d_frame);
    if (d_frm_buf) mpp_buffer_put(d_frm_buf);
    if (d_frm_grp) mpp_buffer_group_put(d_frm_grp);
    if (d_pkt_grp) mpp_buffer_group_put(d_pkt_grp);
    if (d_ctx) mpp_destroy(d_ctx);
    free(nv12_buf);
    d_frame = NULL;
    d_frm_buf = NULL;
    d_frm_grp = NULL;
    d_pkt_grp = NULL;
    d_ctx = NULL;
    d_api = NULL;
    nv12_buf = NULL;
    nv12_cap = 0;
    inited = 0;
}

