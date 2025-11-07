#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "video_encoder.h"
#include "log.h"

extern const encoder_ops_t* encoder_get_x264_ops(void);

#ifdef JETKVM_HAVE_MPP
extern const encoder_ops_t* encoder_get_mpp_ops(void);
#endif

const encoder_ops_t* encoder_get_ops(encoder_type_t type)
{
    switch (type) {
        case ENCODER_TYPE_X264:
            return encoder_get_x264_ops();
        
        case ENCODER_TYPE_MPP:
#ifdef JETKVM_HAVE_MPP
            return encoder_get_mpp_ops();
#else
            log_error("encoder: MPP not available, built without RK support");
            return NULL;
#endif
        
        default:
            log_error("encoder: unknown encoder type %d", type);
            return NULL;
    }
}

encoder_type_t encoder_type_from_name(const char *name)
{
    if (!name || !name[0]) {
        return ENCODER_TYPE_X264;
    }
    
    if (strcasecmp(name, "x264") == 0) {
        return ENCODER_TYPE_X264;
    } else if (strcasecmp(name, "mpp") == 0) {
#ifdef JETKVM_HAVE_MPP
        return ENCODER_TYPE_MPP;
#else
        log_warn("encoder: MPP not available, using x264");
        return ENCODER_TYPE_X264;
#endif
    } else {
        log_warn("encoder: unknown encoder name '%s', using x264", name);
        return ENCODER_TYPE_X264;
    }
}

void encoder_packet_free(encoded_packet_t *packet)
{
    if (packet && packet->data) {
        free(packet->data);
        packet->data = NULL;
        packet->size = 0;
    }
}

