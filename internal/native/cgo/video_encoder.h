#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 编码器类型
typedef enum {
    ENCODER_TYPE_X264 = 0,
    ENCODER_TYPE_MPP  = 1
} encoder_type_t;

// 像素格式
typedef enum {
    PIXEL_FORMAT_I420 = 0,  // YUV420 planar (Y, U, V separate)
    PIXEL_FORMAT_NV12 = 1,  // YUV420 semi-planar (Y, UV interleaved)
    PIXEL_FORMAT_YUYV = 2   // YUV422 packed (YUYV interleaved)
} pixel_format_t;

// 编码器配置
typedef struct {
    int width;
    int height;
    int fps;
    int bitrate_kbps;
    int keyint;           // GOP size
    int repeat_headers;   // 1=每个IDR重复SPS/PPS
    pixel_format_t input_format; // 输入格式（默认 NV12）
    
    // x264 specific
    char x264_preset[32];
    char x264_tune[32];
    char x264_profile[32];
} encoder_config_t;

// 视频帧数据
typedef struct {
    uint8_t *data;
    size_t size;
    pixel_format_t format;
    int width;
    int height;
    int stride_y;         // Y平面步长
    int stride_uv;        // UV平面步长（I420则是U/V分别的步长）
    uint64_t pts_us;      // 时间戳（微秒）

    // 当存在硬件解码或外部组件提供的 MppFrame/MppBuffer 时，
    // 填充下列句柄，编码器可直接使用外部缓冲区，避免CPU memcpy。
    // 其他平台或未使用零拷贝时保持为 NULL。
    void *rk_mpp_frame;   // 指向 MppFrame 的指针（可为空）
    void *rk_mpp_buffer;  // 指向 MppBuffer 的指针（可为空）
} video_frame_t;

// 编码后的数据（H.264 NAL units）
typedef struct {
    uint8_t *data;        // 编码数据（调用者负责释放）
    size_t size;
    int is_keyframe;
    uint64_t pts_us;
} encoded_packet_t;

// 编码器操作接口
typedef struct encoder_ops_s {
    // 初始化编码器
    int (*init)(const encoder_config_t *config, void **ctx);
    
    // 编码一帧（输出packet需要调用者free）
    int (*encode)(void *ctx, const video_frame_t *frame, encoded_packet_t *packet);
    
    // 刷新编码器（获取延迟帧，可为NULL）
    int (*flush)(void *ctx, encoded_packet_t *packet);
    
    // 动态调整参数（可选）
    int (*reconfigure)(void *ctx, const encoder_config_t *config);
    
    // 获取编码器期望的输入格式
    pixel_format_t (*get_input_format)(void *ctx);
    
    // 销毁编码器
    void (*destroy)(void **ctx);
    
    // 编码器名称（用于日志）
    const char *name;
} encoder_ops_t;

// 工厂函数：根据类型获取编码器ops
const encoder_ops_t* encoder_get_ops(encoder_type_t type);

// 便捷函数：根据名称获取类型
encoder_type_t encoder_type_from_name(const char *name);

// 便捷函数：释放编码packet
void encoder_packet_free(encoded_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_ENCODER_H
