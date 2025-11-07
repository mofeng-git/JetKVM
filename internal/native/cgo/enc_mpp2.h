// Simple MPP-based H.264 encoder (copy NV12 into internal MPP buffer)
// Independent from x264/RK_MPI_VENC input MB management.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 on success
int enc_mpp2_init(int width, int height, int fps, int bitrate_kbps, int gop);

// Copy NV12 (width*height*3/2 bytes) into internal buffer, encode one frame, and
// push Annex-B H.264 to video_send_frame. Returns 0 on success.
int enc_mpp2_encode_nv12(const uint8_t *nv12, size_t bytes, uint64_t pts_usec);

void enc_mpp2_shutdown();

#ifdef __cplusplus
}
#endif

