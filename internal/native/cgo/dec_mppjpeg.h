// Minimal MJPEG -> NV12 decoder using Rockchip MPP

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Zero-copy decoded frame info (holds reference to MPP frame)
typedef struct {
    void *mpp_frame;      // MppFrame (opaque)
    void *mpp_buffer;     // MppBuffer (opaque)
    uint8_t *data_ptr;    // Direct pointer to NV12 data
    int width;
    int height;
    int hor_stride;
    int ver_stride;
} dec_mppjpeg_frame_t;

int dec_mppjpeg_init();

// Legacy: Decode to tight NV12 buffer (copies data)
int dec_mppjpeg_decode(const uint8_t *data, size_t len,
                       uint8_t **out_nv12, size_t *out_size,
                       int *out_w, int *out_h);

// Zero-copy: Decode to MPP frame (no copy, returns frame reference)
// Caller must NOT free the frame, it's managed by decoder
int dec_mppjpeg_decode_zero_copy(const uint8_t *data, size_t len,
                                  dec_mppjpeg_frame_t *out_frame);

void dec_mppjpeg_deinit();

#ifdef __cplusplus
}
#endif

