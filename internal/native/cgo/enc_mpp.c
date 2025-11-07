#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>


#include <rk_type.h>
#include <rk_mpi.h>
#include "enc_mpp.h"
#include "ctrl.h"   // video_send_frame
#include "log.h"    // log_*

// Keep this encoder wrapper independent from any capture backend and x264.
// Only MPP/VENC and the output callback (video_send_frame) are touched here.

static const RK_S32 ENC_MPP_CHANNEL = 1; // avoid clashing with other users (video.c uses 0)

static volatile bool g_running = false;
static pthread_t g_read_thread;

static inline RK_U64 get_us()
{
    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (RK_U64)ts.tv_sec * 1000000ULL + (RK_U64)ts.tv_nsec / 1000ULL;
}

static void *venc_read_stream(void *arg)
{
    (void)arg;
    VENC_STREAM_S stFrame;
    memset(&stFrame, 0, sizeof(stFrame));
    stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
    if (!stFrame.pstPack) {
        log_error("enc_mpp: allocate VENC_PACK failed");
        return NULL;
    }

    while (g_running) {
        int ret = RK_MPI_VENC_GetStream(ENC_MPP_CHANNEL, &stFrame, 200);
        if (ret == RK_SUCCESS) {
            void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
            if (pData && stFrame.pstPack->u32Len > 0) {
                video_send_frame((const uint8_t*)pData, (ssize_t)stFrame.pstPack->u32Len);
            }
            ret = RK_MPI_VENC_ReleaseStream(ENC_MPP_CHANNEL, &stFrame);
            if (ret != RK_SUCCESS) {
                log_warn("enc_mpp: ReleaseStream failed %x", ret);
            }
        } else if (ret == RK_ERR_VENC_BUF_EMPTY) {
            // no output within timeout; continue
            continue;
        } else if (ret != RK_SUCCESS) {
            log_error("enc_mpp: GetStream failed %x", ret);
            break;
        }
    }

    free(stFrame.pstPack);
    log_info("enc_mpp: read thread exit");
    return NULL;
}

int enc_mpp_init(int width, int height, int fps,
                 int bitrate_kbps, int max_bitrate_kbps, int gop)
{
    // Assume RK_MPI_SYS_Init is called by the platform init (see video.c)
    // This wrapper will only create the VENC channel and its read thread.
    VENC_CHN_ATTR_S attr; memset(&attr, 0, sizeof(attr));

    attr.stVencAttr.enType = RK_VIDEO_ID_AVC; // H.264
    attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP; // Expect NV12 from caller
    attr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
    attr.stVencAttr.u32PicWidth = (RK_U32)width;
    attr.stVencAttr.u32PicHeight = (RK_U32)height;
    attr.stVencAttr.u32VirWidth = (RK_U32)((width + 1) & ~1);
    attr.stVencAttr.u32VirHeight = (RK_U32)((height + 1) & ~1);
    attr.stVencAttr.u32StreamBufCnt = 3;
    attr.stVencAttr.u32BufSize = (RK_U32)(width * height * 3 / 2);
    attr.stVencAttr.enMirror = MIRROR_NONE;

    attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
    attr.stRcAttr.stH264Vbr.u32BitRate = (RK_U32)bitrate_kbps;
    attr.stRcAttr.stH264Vbr.u32MaxBitRate = (RK_U32)(max_bitrate_kbps > 0 ? max_bitrate_kbps : bitrate_kbps * 2);
    attr.stRcAttr.stH264Vbr.u32Gop = (RK_U32)(gop > 0 ? gop : 60);

    int ret = RK_MPI_VENC_CreateChn(ENC_MPP_CHANNEL, &attr);
    if (ret != RK_SUCCESS) {
        log_error("enc_mpp: CreateChn failed %d", ret);
        return -1;
    }

    VENC_RECV_PIC_PARAM_S recv; memset(&recv, 0, sizeof(recv));
    recv.s32RecvPicNum = -1; // continuous
    ret = RK_MPI_VENC_StartRecvFrame(ENC_MPP_CHANNEL, &recv);
    if (ret != RK_SUCCESS) {
        log_error("enc_mpp: StartRecvFrame failed %d", ret);
        (void)RK_MPI_VENC_DestroyChn(ENC_MPP_CHANNEL);
        return -1;
    }

    g_running = true;
    int rc = pthread_create(&g_read_thread, NULL, venc_read_stream, NULL);
    if (rc != 0) {
        g_running = false;
        log_error("enc_mpp: pthread_create failed: %s", strerror(rc));
        (void)RK_MPI_VENC_StopRecvFrame(ENC_MPP_CHANNEL);
        (void)RK_MPI_VENC_DestroyChn(ENC_MPP_CHANNEL);
        return -1;
    }

    log_info("enc_mpp: initialized %dx%d@%dfps bitrate=%dkbps gop=%d", width, height, fps, bitrate_kbps, gop);
    return 0;
}

int enc_mpp_send_frame(VIDEO_FRAME_INFO_S *frame, int timeout_ms)
{
    if (!frame) return -1;
    if (!g_running) return -1;
    // Ensure some basic fields set
    if (frame->stVFrame.enPixelFormat == 0) {
        frame->stVFrame.enPixelFormat = RK_FMT_YUV420SP; // NV12
    }
    if (frame->stVFrame.u64PTS == 0) {
        frame->stVFrame.u64PTS = get_us();
    }
    int to = (timeout_ms < 0) ? -1 : timeout_ms;
    int ret = RK_MPI_VENC_SendFrame(ENC_MPP_CHANNEL, frame, to);
    if (ret != RK_SUCCESS) {
        log_warn("enc_mpp: SendFrame failed %x", ret);
        return -1;
    }
    return 0;
}

void enc_mpp_shutdown()
{
    if (!g_running) {
        // still ensure channel is cleaned up if it exists
        (void)RK_MPI_VENC_StopRecvFrame(ENC_MPP_CHANNEL);
        (void)RK_MPI_VENC_DestroyChn(ENC_MPP_CHANNEL);
        return;
    }
    g_running = false;
    // Let GetStream timeout wake the thread
    pthread_join(g_read_thread, NULL);
    (void)RK_MPI_VENC_StopRecvFrame(ENC_MPP_CHANNEL);
    (void)RK_MPI_VENC_DestroyChn(ENC_MPP_CHANNEL);
    log_info("enc_mpp: shutdown");
}
