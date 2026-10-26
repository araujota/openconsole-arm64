#include "openconsole/details_input.h"

#include <stddef.h>

static int allowed(const oc_details_input *state, oc_details_target target) {
    if (!state->open) return 0;
    if (state->confirming_delete)
        return target == OC_DETAILS_CANCEL || target == OC_DETAILS_CONFIRM;
    return target == OC_DETAILS_CLOSE || target == OC_DETAILS_LAUNCH ||
           target == OC_DETAILS_DELETE;
}

static oc_details_effect activate(oc_details_input *state,
                                  oc_details_target target,
                                  int launch_enabled) {
    if (target == OC_DETAILS_CLOSE) state->open = 0u;
    else if (target == OC_DETAILS_DELETE) state->confirming_delete = 1u;
    else if (target == OC_DETAILS_CANCEL) state->confirming_delete = 0u;
    else if (target == OC_DETAILS_CONFIRM) return OC_DETAILS_EFFECT_DELETE;
    else if (target == OC_DETAILS_LAUNCH && launch_enabled)
        return OC_DETAILS_EFFECT_LAUNCH;
    return OC_DETAILS_EFFECT_NONE;
}

oc_details_effect oc_details_pointer(oc_details_input *state,
                                     oc_details_target target,
                                     int button_down, int launch_enabled) {
    oc_details_target pressed = (oc_details_target)state->pressed;
    if (button_down) {
        state->pressed = (uint8_t)(allowed(state, target) ? target : OC_DETAILS_NONE);
        return OC_DETAILS_EFFECT_NONE;
    }
    state->pressed = OC_DETAILS_NONE;
    if (target != pressed || !allowed(state, target)) return OC_DETAILS_EFFECT_NONE;
    return activate(state, target, launch_enabled);
}

oc_details_effect oc_details_key(oc_details_input *state, uint16_t character,
                                 int launch_enabled) {
    if (!state->open) return OC_DETAILS_EFFECT_NONE;
    if (character == 27u) {
        if (state->confirming_delete) state->confirming_delete = 0u;
        else state->open = 0u;
    } else if (state->confirming_delete &&
               (character == 'y' || character == 'Y' || character == 13u)) {
        return OC_DETAILS_EFFECT_DELETE;
    } else if (state->confirming_delete &&
               (character == 'n' || character == 'N')) {
        state->confirming_delete = 0u;
    } else if (!state->confirming_delete &&
               (character == 'd' || character == 'D')) {
        state->confirming_delete = 1u;
    } else if (!state->confirming_delete && character == 13u && launch_enabled) {
        return OC_DETAILS_EFFECT_LAUNCH;
    }
    return OC_DETAILS_EFFECT_NONE;
}

size_t oc_details_launch_index(size_t modal_index, size_t game_count,
                               int supported) {
    return supported && modal_index < game_count ? modal_index : SIZE_MAX;
}
