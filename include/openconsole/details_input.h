#ifndef OPENCONSOLE_DETAILS_INPUT_H
#define OPENCONSOLE_DETAILS_INPUT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    OC_DETAILS_NONE = 0,
    OC_DETAILS_CLOSE,
    OC_DETAILS_LAUNCH,
    OC_DETAILS_DELETE,
    OC_DETAILS_CANCEL,
    OC_DETAILS_CONFIRM
} oc_details_target;

typedef enum {
    OC_DETAILS_EFFECT_NONE = 0,
    OC_DETAILS_EFFECT_LAUNCH,
    OC_DETAILS_EFFECT_DELETE
} oc_details_effect;

typedef struct {
    uint8_t open;
    uint8_t confirming_delete;
    uint8_t pressed;
} oc_details_input;

oc_details_effect oc_details_pointer(oc_details_input *state,
                                     oc_details_target target,
                                     int button_down, int launch_enabled);
oc_details_effect oc_details_key(oc_details_input *state, uint16_t character,
                                 int launch_enabled);
size_t oc_details_launch_index(size_t modal_index, size_t game_count,
                               int supported);

#endif
