// Minimal Rockchip MPP H.264 encoder wrapper (independent from x264)
// This module exposes a tiny API that other capture backends can call.
// It deliberately does NOT own any capture or color-conversion logic.
//
// Usage (NV12 path expected):
//   enc_mpp_init(width, height, fps, bitrate_kbps, max_bitrate_kbps, gop);
//   // For each frame, prepare a VIDEO_FRAME_INFO_S with NV12 (RK_FMT_YUV420SP)
//   // and an MB_BLK that stays valid until the encoder consumes it.
//   enc_mpp_send_frame(&frame, 2000 /*ms*/);
//   enc_mpp_shutdown();

#pragma once

#include <rk_type.h>
#include <rk_mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize and start a single H.264 VENC channel.
// Returns 0 on success, negative on failure.
int enc_mpp_init(int width, int height, int fps,
                 int bitrate_kbps, int max_bitrate_kbps, int gop);

// Send one frame to VENC. The caller must populate:
//   frame.stVFrame.pMbBlk  -> NV12 (RK_FMT_YUV420SP) buffer
//   frame.stVFrame.u32Width/u32Height/PTS/etc
// timeout_ms: -1 to block forever, 0 non-blocking, >0 to block up to N ms
int enc_mpp_send_frame(VIDEO_FRAME_INFO_S *frame, int timeout_ms);

// Stop the read thread and destroy the channel.
void enc_mpp_shutdown();

#ifdef __cplusplus
}
#endif

