#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "ctrl.h"   // video_report_format, video_send_frame
#include "log.h"    // log_info, log_warn, log_error, log_trace
#include "uvc_backend.h" // function prototypes for start/stop/reinit

#include <x264.h>
#include <turbojpeg.h>

// Simple V4L2 MMAP buffer
struct uvc_buf { void *start; size_t length; };

static char uvc_dev_path[256] = "/dev/video1";
static int uvc_width = 1280;
static int uvc_height = 720;
static int uvc_fps = 15;
static int uvc_fd = -1;
static struct uvc_buf *uvc_buffers = NULL;
static int uvc_nbufs = 0;
static pthread_t uvc_thread;
static int uvc_running = 0;
static pthread_mutex_t uvc_mutex = PTHREAD_MUTEX_INITIALIZER;

// JPEG decoder
static tjhandle tj = NULL;
static unsigned char *yuv_buf = NULL; // I420 contiguous buffer
static size_t yuv_buf_size = 0;
// MJPEG source YUV buffer as produced by TurboJPEG (sampling may be 420/422/444)
static unsigned char *yuv_src = NULL;
static size_t yuv_src_size = 0;
static int mjpeg_subsamp = -1; // TJSAMP_*
static int mjpeg_colorspace = -1;

// x264 encoder (default). If JETKVM_ENCODER=mpp and libmpp is integrated in this
// build in the future, we can add an alternate path. For now, fall back to x264.
static x264_t *x264 = NULL;
static x264_param_t x264_param;
static x264_picture_t x_pic_in;
static int repeat_headers = 1;
static int bitrate_kbps = 2000;
static int keyint = 60;
static char x264_preset[32] = "ultrafast";
static char x264_tune[32] = "zerolatency";
static char x264_profile[32] = "baseline";

// input pixel format selection
static uint32_t uvc_pixfmt = V4L2_PIX_FMT_MJPEG; // default MJPG


static int xioctl(int fd, int request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
    return r;
}


static void warn_tj_once_per_sec() {
    static time_t last = 0;
    static int suppressed = 0;
    time_t now = time(NULL);
    if (now == last) { suppressed++; return; }
    if (suppressed > 0) {
        log_warn("UVC: tjDecompressToYUV2 failed (suppressed %d)", suppressed);
    } else {
        log_warn("UVC: tjDecompressToYUV2 failed");
    }
    last = now; suppressed = 0;
}


static int uvc_open_device() {
    uvc_fd = open(uvc_dev_path, O_RDWR | O_NONBLOCK, 0);
    if (uvc_fd < 0) {
        log_error("UVC: failed to open %s: %s", uvc_dev_path, strerror(errno));
        return -1;
    }
    log_info("UVC: opened %s", uvc_dev_path);
    return 0;
}

static int uvc_set_format() {
    struct v4l2_format fmt; memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = uvc_width;
    fmt.fmt.pix.height = uvc_height;
    fmt.fmt.pix.pixelformat = uvc_pixfmt;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(uvc_fd, VIDIOC_S_FMT, &fmt) < 0) {
        log_error("UVC: VIDIOC_S_FMT failed: %s", strerror(errno));
        return -1;
    }
    // Try set FPS
    struct v4l2_streamparm parm; memset(&parm, 0, sizeof parm);
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = uvc_fps;
    xioctl(uvc_fd, VIDIOC_S_PARM, &parm); // best effort
    return 0;
}

static int uvc_init_mmap() {
    struct v4l2_requestbuffers req; memset(&req, 0, sizeof req);
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(uvc_fd, VIDIOC_REQBUFS, &req) < 0) {
        log_error("UVC: VIDIOC_REQBUFS failed: %s", strerror(errno));
        return -1;
    }
    if (req.count < 2) {
        log_error("UVC: insufficient buffer memory");
        return -1;
    }
    uvc_buffers = calloc(req.count, sizeof *uvc_buffers);
    if (!uvc_buffers) return -1;
    for (uvc_nbufs = 0; uvc_nbufs < (int)req.count; uvc_nbufs++) {
        struct v4l2_buffer buf; memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = uvc_nbufs;
        if (xioctl(uvc_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            log_error("UVC: VIDIOC_QUERYBUF failed: %s", strerror(errno));
            return -1;
        }
        uvc_buffers[uvc_nbufs].length = buf.length;
        uvc_buffers[uvc_nbufs].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, uvc_fd, buf.m.offset);
        if (uvc_buffers[uvc_nbufs].start == MAP_FAILED) {
            log_error("UVC: mmap failed: %s", strerror(errno));
            return -1;
        }
    }
    // queue all
    for (int i = 0; i < uvc_nbufs; i++) {
        struct v4l2_buffer buf; memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(uvc_fd, VIDIOC_QBUF, &buf) < 0) {
            log_error("UVC: VIDIOC_QBUF failed: %s", strerror(errno));
            return -1;
        }
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(uvc_fd, VIDIOC_STREAMON, &type) < 0) {
        log_error("UVC: VIDIOC_STREAMON failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int init_turbojpeg() {
    tj = tjInitDecompress();
    if (!tj) {
        log_error("UVC: tjInitDecompress failed");
        return -1;
    }
    // allocate yuv buffer for I420 with minimal padding (1)
    yuv_buf_size = (size_t)uvc_width * (size_t)uvc_height * 3 / 2;
    yuv_buf = (unsigned char*)malloc(yuv_buf_size);
    if (!yuv_buf) return -1;
    return 0;
}

static void yuyv_to_i420(const unsigned char *src, int width, int height,
                         unsigned char *dst_y, unsigned char *dst_u, unsigned char *dst_v,
                         int stride_y, int stride_uv) {
    // width, height should be even
    int w2 = width >> 1;
    for (int y = 0; y < height; y += 2) {
        const unsigned char *row0 = src + y * width * 2;
        const unsigned char *row1 = row0 + width * 2;
        unsigned char *Y0 = dst_y + y * stride_y;
        unsigned char *Y1 = Y0 + stride_y;
        unsigned char *U = dst_u + (y >> 1) * stride_uv;
        unsigned char *V = dst_v + (y >> 1) * stride_uv;
        for (int x = 0; x < w2; ++x) {
            // row0: Y0 U0 Y1 V0
            int idx = x * 4;
            unsigned char y00 = row0[idx + 0];
            unsigned char u0  = row0[idx + 1];
            unsigned char y01 = row0[idx + 2];
            unsigned char v0  = row0[idx + 3];
            // row1: Y2 U1 Y3 V1
            unsigned char y10 = row1[idx + 0];
            unsigned char u1  = row1[idx + 1];
            unsigned char y11 = row1[idx + 2];
            unsigned char v1  = row1[idx + 3];

            // Write Y
            Y0[x*2 + 0] = y00;
            Y0[x*2 + 1] = y01;
            Y1[x*2 + 0] = y10;
            Y1[x*2 + 1] = y11;

            // Average U/V over 2x2 block
            U[x] = (unsigned char)(((int)u0 + (int)u1 + 1) >> 1);
            V[x] = (unsigned char)(((int)v0 + (int)v1 + 1) >> 1);
        }
    }
}

static int init_x264() {
    x264_param_default_preset(&x264_param, x264_preset, x264_tune);
    x264_param.i_csp = X264_CSP_I420;
    x264_param.i_width = uvc_width;
    x264_param.i_height = uvc_height;
    x264_param.i_fps_num = uvc_fps;
    x264_param.i_fps_den = 1;
    x264_param.i_keyint_max = keyint;
    x264_param.b_repeat_headers = repeat_headers;
    x264_param.b_annexb = 1; // Annex B for RTP packaging
    x264_param.b_vfr_input = 0;
    x264_param.rc.i_rc_method = X264_RC_ABR;
    x264_param.rc.i_bitrate = bitrate_kbps;
    x264_param.i_threads = 0; // auto
    if (x264_param_apply_profile(&x264_param, x264_profile) < 0) {
        log_warn("UVC: failed to apply x264 profile %s, continuing", x264_profile);
    }
    x264 = x264_encoder_open(&x264_param);
    if (!x264) {
        log_error("UVC: x264_encoder_open failed");
        return -1;
    }
    if (x264_picture_alloc(&x_pic_in, X264_CSP_I420, uvc_width, uvc_height) < 0) {
        log_error("UVC: x264_picture_alloc failed");
        return -1;
    }
    return 0;
}

static void* uvc_thread_main(void* arg) {
    (void)arg;
    log_info("UVC: streaming thread started");
    // publish video state once
    video_report_format(true, NULL, (u_int16_t)uvc_width, (u_int16_t)uvc_height, (double)uvc_fps);
    while (uvc_running) {
        fd_set fds; FD_ZERO(&fds); FD_SET(uvc_fd, &fds);
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        int r = select(uvc_fd + 1, &fds, NULL, NULL, &tv);
        if (r == -1) {
            if (errno == EINTR) continue;
            log_error("UVC: select failed: %s", strerror(errno));
            break;
        } else if (r == 0) {
            continue; // timeout
        }

        struct v4l2_buffer buf; memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(uvc_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            log_error("UVC: VIDIOC_DQBUF failed: %s", strerror(errno));
            break;
        }

        if (uvc_pixfmt == V4L2_PIX_FMT_MJPEG) {
            unsigned char* mjpg_ptr = (unsigned char*)uvc_buffers[buf.index].start;
            unsigned long mjpg_size = buf.bytesused;
            int flags = 0;
            // Parse subsampling on first frame and allocate buffers accordingly
            int iw=uvc_width, ih=uvc_height; // image width/height from header
            if (mjpeg_subsamp < 0) {
                int subs=0, cs=0;
                if (tjDecompressHeader3(tj, mjpg_ptr, mjpg_size, &iw, &ih, &subs, &cs) != 0) {
                    warn_tj_once_per_sec();
                    (void)xioctl(uvc_fd, VIDIOC_QBUF, &buf);
                    continue;
                }
                mjpeg_subsamp = subs; mjpeg_colorspace = cs;
                // If device ignored S_FMT and delivered different size, adapt encoder/buffers
                if (iw != uvc_width || ih != uvc_height) {
                    log_warn("UVC: MJPEG header size %dx%d != requested %dx%d, adapting",
                             iw, ih, uvc_width, uvc_height);
                    // re-init x264 with new size
                    uvc_width = iw; uvc_height = ih;
                    if (x264) { x264_encoder_close(x264); x264 = NULL; }
                    x264_picture_clean(&x_pic_in);
                    if (yuv_buf) { free(yuv_buf); yuv_buf = NULL; }
                    if (init_x264() < 0) { log_error("UVC: re-init x264 failed"); break; }
                    video_report_format(true, NULL, (u_int16_t)uvc_width, (u_int16_t)uvc_height, (double)uvc_fps);
                }
                if (!yuv_buf) {
                    yuv_buf_size = (size_t)uvc_width * (size_t)uvc_height * 3 / 2;
                    yuv_buf = (unsigned char*)malloc(yuv_buf_size);
                    if (!yuv_buf) { log_error("UVC: malloc yuv_buf failed"); break; }
                }
                // Always allocate yuv_src for TurboJPEG YUV output regardless of subsampling
                // so that tjDecompressToYUV2 always has a valid destination.
                if (yuv_src) { free(yuv_src); yuv_src = NULL; }
                yuv_src_size = tjBufSizeYUV2(iw, 1, ih, mjpeg_subsamp);
                yuv_src = (unsigned char*)malloc(yuv_src_size);
                if (!yuv_src) { log_error("UVC: malloc yuv_src failed"); break; }
                log_info("UVC: MJPEG subsamp=%d colorspace=%d", mjpeg_subsamp, mjpeg_colorspace);
            }
            // Decompress with actual header size (iw/ih); if not known (subsample already set), assume uvc_width/height
            if (mjpeg_subsamp >= 0) { iw = uvc_width; ih = uvc_height; }
            if (tjDecompressToYUV2(tj, mjpg_ptr, mjpg_size, yuv_src, iw, 1, ih, flags) != 0) {
                warn_tj_once_per_sec();
                (void)xioctl(uvc_fd, VIDIOC_QBUF, &buf);
                continue;
            }
            // Convert to I420 in yuv_buf
            unsigned char* Yd = yuv_buf;
            unsigned char* Ud = Yd + uvc_width * uvc_height;
            unsigned char* Vd = Ud + (uvc_width/2) * (uvc_height/2);
            unsigned char* Ys = yuv_src;
            memcpy(Yd, Ys, (size_t)uvc_width * uvc_height);
            if (mjpeg_subsamp == TJSAMP_420) {
                int cw = uvc_width/2, ch = uvc_height/2;
                unsigned char* Us = Ys + (uvc_width * uvc_height);
                unsigned char* Vs = Us + cw * ch;
                memcpy(Ud, Us, (size_t)cw * ch);
                memcpy(Vd, Vs, (size_t)cw * ch);
            } else if (mjpeg_subsamp == TJSAMP_422) {
                int cw = uvc_width/2;
                unsigned char* Us = Ys + (uvc_width * uvc_height);
                unsigned char* Vs = Us + cw * uvc_height; // 4:2:2 has full chroma height
                for (int y2 = 0; y2 < uvc_height; y2 += 2) {
                    unsigned char* u0 = Us + y2 * cw;
                    unsigned char* u1 = u0 + cw;
                    unsigned char* v0 = Vs + y2 * cw;
                    unsigned char* v1 = v0 + cw;
                    unsigned char* du = Ud + (y2/2) * cw;
                    unsigned char* dv = Vd + (y2/2) * cw;
                    for (int x2 = 0; x2 < cw; ++x2) {
                        du[x2] = (unsigned char)(((int)u0[x2] + (int)u1[x2] + 1) >> 1);
                        dv[x2] = (unsigned char)(((int)v0[x2] + (int)v1[x2] + 1) >> 1);
                    }
                }
            } else { // TJSAMP_444 and others -> box filter 2x2
                int cw = uvc_width/2;
                unsigned char* Us = Ys + (uvc_width * uvc_height);
                unsigned char* Vs = Us + uvc_width * uvc_height;
                for (int y2 = 0; y2 < uvc_height; y2 += 2) {
                    unsigned char* u0 = Us + y2 * uvc_width;
                    unsigned char* u1 = u0 + uvc_width;
                    unsigned char* v0 = Vs + y2 * uvc_width;
                    unsigned char* v1 = v0 + uvc_width;
                    unsigned char* du = Ud + (y2/2) * cw;
                    unsigned char* dv = Vd + (y2/2) * cw;
                    for (int x2 = 0; x2 < uvc_width; x2 += 2) {
                        int uavg = u0[x2] + u0[x2+1] + u1[x2] + u1[x2+1];
                        int vavg = v0[x2] + v0[x2+1] + v1[x2] + v1[x2+1];
                        du[x2/2] = (unsigned char)((uavg + 2) >> 2);
                        dv[x2/2] = (unsigned char)((vavg + 2) >> 2);
                    }
                }
            }
        } else { // YUYV path
            const unsigned char *yuyv = (const unsigned char*)uvc_buffers[buf.index].start;
            unsigned char* Y = yuv_buf;
            unsigned char* U = Y + uvc_width * uvc_height;
            unsigned char* V = U + (uvc_width/2) * (uvc_height/2);
            yuyv_to_i420(yuyv, uvc_width, uvc_height, Y, U, V, uvc_width, uvc_width/2);
        }

        // Map yuv_buf into x264_picture planes (I420 planar)
        unsigned char* y = yuv_buf;
        unsigned char* u = y + (uvc_width * uvc_height);
        unsigned char* v = u + (uvc_width/2) * (uvc_height/2);
       
        x_pic_in.img.plane[0] = y;
        x_pic_in.img.plane[1] = u;
        x_pic_in.img.plane[2] = v;
        x_pic_in.img.i_stride[0] = uvc_width;
        x_pic_in.img.i_stride[1] = uvc_width/2;
        x_pic_in.img.i_stride[2] = uvc_width/2;

        x264_nal_t *nals = NULL; int i_nals = 0; x264_picture_t pic_out;
        int bytes = x264_encoder_encode(x264, &nals, &i_nals, &x_pic_in, &pic_out);
        if (bytes < 0) {
            log_warn("UVC: x264_encoder_encode failed");
        } else if (bytes > 0) {
            // Concatenate NALs to a single buffer
            int total = 0; for (int i=0;i<i_nals;i++) total += nals[i].i_payload;
            unsigned char *out = (unsigned char*)malloc(total);
            if (out) {
                int off = 0; for (int i=0;i<i_nals;i++) { memcpy(out+off, nals[i].p_payload, nals[i].i_payload); off += nals[i].i_payload; }
                video_send_frame(out, (ssize_t)total);
                free(out);
            }
        }

        if (xioctl(uvc_fd, VIDIOC_QBUF, &buf) < 0) {
            log_error("UVC: VIDIOC_QBUF failed: %s", strerror(errno));
            break;
        }
    }
    log_info("UVC: streaming thread exiting");
    return NULL;
}

int uvc_init_from_env() {
    const char* s;
    s = getenv("JETKVM_UVC_DEVICE"); if (s && s[0]) { strncpy(uvc_dev_path, s, sizeof(uvc_dev_path)-1); uvc_dev_path[sizeof(uvc_dev_path)-1] = '\0'; }
    s = getenv("JETKVM_UVC_WIDTH"); if (s) { int v = atoi(s); if (v>0) uvc_width = v; }
    s = getenv("JETKVM_UVC_HEIGHT"); if (s) { int v = atoi(s); if (v>0) uvc_height = v; }
    s = getenv("JETKVM_UVC_FPS"); if (s) { int v = atoi(s); if (v>0) uvc_fps = v; }
    s = getenv("JETKVM_UVC_FORMAT"); if (s && s[0]) { if (strcasecmp(s, "YUYV")==0 || strcasecmp(s, "YUY2")==0) uvc_pixfmt = V4L2_PIX_FMT_YUYV; else uvc_pixfmt = V4L2_PIX_FMT_MJPEG; }
    s = getenv("JETKVM_BITRATE_KBPS"); if (s) { int v = atoi(s); if (v>0) bitrate_kbps = v; }
    s = getenv("JETKVM_KEYINT"); if (s) { int v = atoi(s); if (v>0) keyint = v; }
    s = getenv("JETKVM_REPEAT_HEADERS"); if (s) { int v = atoi(s); repeat_headers = (v!=0); }
    s = getenv("JETKVM_X264_PRESET"); if (s && s[0]) { strncpy(x264_preset, s, sizeof(x264_preset)-1); }
    s = getenv("JETKVM_X264_TUNE"); if (s && s[0]) { strncpy(x264_tune, s, sizeof(x264_tune)-1); }
    s = getenv("JETKVM_X264_PROFILE"); if (s && s[0]) { strncpy(x264_profile, s, sizeof(x264_profile)-1); }
    // Encoder selection placeholder: accept "mpp" but fall back to x264 if not available
    s = getenv("JETKVM_ENCODER");
    if (s && s[0]) {
        if (strcasecmp(s, "mpp") == 0) {
            log_warn("UVC: encoder=mpp requested but not available in this build; falling back to x264");
        }
    }
    

    if (uvc_open_device() < 0) return -1;
    if (uvc_set_format() < 0) return -1;
    if (uvc_init_mmap() < 0) return -1;
    if (uvc_pixfmt == V4L2_PIX_FMT_MJPEG) {
        if (init_turbojpeg() < 0) return -1;
    } else {
        // allocate YUV buffer for YUYV conversion
        yuv_buf_size = (size_t)uvc_width * (size_t)uvc_height * 3 / 2;
        yuv_buf = (unsigned char*)malloc(yuv_buf_size);
        if (!yuv_buf) return -1;
    }
    if (init_x264() < 0) return -1;
    log_info("UVC: initialized device=%s %dx%d@%dfps fmt=%s bitrate=%dkbps",
             uvc_dev_path, uvc_width, uvc_height, uvc_fps,
             (uvc_pixfmt==V4L2_PIX_FMT_YUYV?"YUYV":"MJPG"), bitrate_kbps);
    return 0;
}

/* runtime re-init helpers removed */

int uvc_start_streaming() {
    pthread_mutex_lock(&uvc_mutex);
    if (uvc_running) { pthread_mutex_unlock(&uvc_mutex); return 0; }
    uvc_running = 1;
    int rc = pthread_create(&uvc_thread, NULL, uvc_thread_main, NULL);
    if (rc != 0) { uvc_running = 0; log_error("UVC: pthread_create failed: %s", strerror(rc)); }
    pthread_mutex_unlock(&uvc_mutex);
    return rc == 0 ? 0 : -1;
}

void uvc_stop_streaming() {
    pthread_mutex_lock(&uvc_mutex);
    if (!uvc_running) { pthread_mutex_unlock(&uvc_mutex); return; }
    uvc_running = 0;
    pthread_mutex_unlock(&uvc_mutex);
    // Wake the thread by select timeout; then join
    pthread_join(uvc_thread, NULL);
}

void uvc_shutdown() {
    uvc_stop_streaming();
    if (x264) { x264_encoder_close(x264); x264 = NULL; }
    x264_picture_clean(&x_pic_in);
    if (tj) { tjDestroy(tj); tj = NULL; }
    if (yuv_buf) { free(yuv_buf); yuv_buf = NULL; }
    if (yuv_src) { free(yuv_src); yuv_src = NULL; }
    if (uvc_fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(uvc_fd, VIDIOC_STREAMOFF, &type);
        for (int i = 0; i < uvc_nbufs; i++) {
            if (uvc_buffers[i].start && uvc_buffers[i].length) munmap(uvc_buffers[i].start, uvc_buffers[i].length);
        }
        free(uvc_buffers); uvc_buffers = NULL; uvc_nbufs = 0;
        close(uvc_fd); uvc_fd = -1;
    }
    log_info("UVC: shutdown completed");
}
