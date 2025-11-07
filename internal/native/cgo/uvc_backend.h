// Minimal UVC backend (MJPEG -> x264) interface
#pragma once

int uvc_init_from_env();
void uvc_shutdown();
int uvc_start_streaming();
void uvc_stop_streaming();
// Re-init x264 encoder with a new target bitrate derived from a quality factor
// in range [0..1]. Returns 0 on success, negative on failure.
/* runtime reinit APIs removed */
