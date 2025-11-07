#include <stdlib.h>
#include <string.h>
#include "ctrl.h"
#include "video.h"
#include "log.h"

static jetkvm_video_state_t g_state;
static jetkvm_video_state_handler_t *g_video_state_handler = NULL;
static jetkvm_rpc_handler_t *g_rpc_handler = NULL;
static jetkvm_video_handler_t *g_video_handler = NULL;
static jetkvm_indev_handler_t *g_indev_handler = NULL;

void jetkvm_set_log_handler(jetkvm_log_handler_t *handler);
void log_set_handler(jetkvm_log_handler_t *handler);

void jetkvm_set_log_handler(jetkvm_log_handler_t *handler) { log_set_handler(handler); }
void jetkvm_set_video_handler(jetkvm_video_handler_t *handler) { g_video_handler = handler; }
void jetkvm_set_indev_handler(jetkvm_indev_handler_t *handler) { g_indev_handler = handler; (void)g_indev_handler; }
void jetkvm_set_rpc_handler(jetkvm_rpc_handler_t *handler) { g_rpc_handler = handler; }
void jetkvm_call_rpc_handler(const char *method, const char *params) { if (g_rpc_handler) g_rpc_handler(method, params); }
void jetkvm_set_video_state_handler(jetkvm_video_state_handler_t *handler) { g_video_state_handler = handler; }

const char *jetkvm_ui_event_code_to_name(int code) { (void)code; return "none"; }

void video_report_format(bool ready, const char *error, u_int16_t width, u_int16_t height, double fps)
{
    g_state.ready = ready; g_state.error = error; g_state.width = width; g_state.height = height; g_state.frame_per_second = fps;
    if (g_video_state_handler) g_video_state_handler(&g_state);
}

int video_send_frame(const uint8_t *frame, ssize_t len)
{
    if (g_video_handler) g_video_handler(frame, len); else log_error("video handler is not set");
    return 0;
}

// UI stubs
void jetkvm_ui_set_var(const char *name, const char *value) { (void)name; (void)value; }
const char *jetkvm_ui_get_var(const char *name) { (void)name; return ""; }
void jetkvm_ui_init(u_int16_t rotation) { (void)rotation; }
void jetkvm_ui_tick() {}
void jetkvm_ui_set_rotation(u_int16_t rotation) { (void)rotation; }
const char *jetkvm_ui_get_current_screen() { return ""; }
void jetkvm_ui_load_screen(const char *obj_name) { (void)obj_name; }
int jetkvm_ui_set_text(const char *obj_name, const char *text) { (void)obj_name; (void)text; return 0; }
void jetkvm_ui_set_image(const char *obj_name, const char *image_name) { (void)obj_name; (void)image_name; }
void jetkvm_ui_add_state(const char *obj_name, const char *state_name) { (void)obj_name; (void)state_name; }
void jetkvm_ui_clear_state(const char *obj_name, const char *state_name) { (void)obj_name; (void)state_name; }
void jetkvm_ui_fade_in(const char *obj_name, u_int32_t duration) { (void)obj_name; (void)duration; }
void jetkvm_ui_fade_out(const char *obj_name, u_int32_t duration) { (void)obj_name; (void)duration; }
void jetkvm_ui_set_opacity(const char *obj_name, u_int8_t opacity) { (void)obj_name; (void)opacity; }
int jetkvm_ui_add_flag(const char *obj_name, const char *flag_name) { (void)obj_name; (void)flag_name; return 0; }
int jetkvm_ui_clear_flag(const char *obj_name, const char *flag_name) { (void)obj_name; (void)flag_name; return 0; }
const char *jetkvm_ui_get_lvgl_version() { return "stub"; }

// EDID stubs
int jetkvm_video_set_edid(const char *edid_hex) { (void)edid_hex; return -1; }
char *jetkvm_video_get_edid_hex() { return NULL; }
char *jetkvm_video_log_status() { return strdup("unsupported"); }
jetkvm_video_state_t *jetkvm_video_get_status() { return &g_state; }

// Video lifecycle
int jetkvm_video_init() { return video_init(); }
void jetkvm_video_shutdown() { video_shutdown(); }
void jetkvm_video_start() { video_start_streaming(); }
void jetkvm_video_stop() { video_stop_streaming(); }
int jetkvm_video_set_quality_factor(float factor) { video_set_quality_factor(factor); return 0; }
float jetkvm_video_get_quality_factor() { return video_get_quality_factor(); }

void jetkvm_crash() { int *p = 0; *p = 0; }

