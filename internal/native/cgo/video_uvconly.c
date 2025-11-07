#include "uvc_backend.h"
#include "video.h"
#include "log.h"

static float quality_factor = 1.0f; // placeholder (not used in UVC-only)

int video_init() {
    // UVC-only build: initialize UVC backend from env/config
    if (uvc_init_from_env() != 0) {
        log_error("UVC-only: failed to initialize UVC backend");
        return -1;
    }
    return 0;
}

void video_shutdown() {
    uvc_shutdown();
}

void video_start_streaming() {
    (void)uvc_start_streaming();
}

void video_stop_streaming() {
    uvc_stop_streaming();
}

void video_set_quality_factor(float factor) {
    // 保留质量因子，仅记录，不触发任何运行时重建/调整
    if (factor < 0) factor = 0; if (factor > 1) factor = 1;
    quality_factor = factor;
}

float video_get_quality_factor() {
    return quality_factor;
}
