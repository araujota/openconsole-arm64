#include "openconsole/details_input.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    oc_details_input state = {1u, 0u, 0u};
    assert(oc_details_pointer(&state, OC_DETAILS_LAUNCH, 1, 1) == OC_DETAILS_EFFECT_NONE);
    assert(oc_details_pointer(&state, OC_DETAILS_NONE, 0, 1) == OC_DETAILS_EFFECT_NONE);
    assert(state.open); /* drag-off launch is inert */
    assert(oc_details_pointer(&state, OC_DETAILS_DELETE, 1, 1) == OC_DETAILS_EFFECT_NONE);
    assert(oc_details_pointer(&state, OC_DETAILS_DELETE, 0, 1) == OC_DETAILS_EFFECT_NONE);
    assert(state.confirming_delete);
    assert(oc_details_pointer(&state, OC_DETAILS_CLOSE, 1, 1) == OC_DETAILS_EFFECT_NONE);
    assert(oc_details_pointer(&state, OC_DETAILS_CLOSE, 0, 1) == OC_DETAILS_EFFECT_NONE);
    assert(state.confirming_delete); /* obscured control cannot escape focus */
    assert(oc_details_key(&state, 27u, 1) == OC_DETAILS_EFFECT_NONE);
    assert(!state.confirming_delete && state.open);
    assert(oc_details_key(&state, 'd', 1) == OC_DETAILS_EFFECT_NONE);
    assert(oc_details_key(&state, 'n', 1) == OC_DETAILS_EFFECT_NONE);
    assert(!state.confirming_delete);
    assert(oc_details_key(&state, 13u, 0) == OC_DETAILS_EFFECT_NONE);
    assert(oc_details_launch_index(3u, 4u, 0) == SIZE_MAX);
    assert(oc_details_launch_index(4u, 4u, 1) == SIZE_MAX);
    assert(oc_details_launch_index(3u, 4u, 1) == 3u);
    assert(oc_details_key(&state, 13u, 1) == OC_DETAILS_EFFECT_LAUNCH);
    assert(oc_details_key(&state, 27u, 1) == OC_DETAILS_EFFECT_NONE);
    assert(!state.open);

    state = (oc_details_input){1u, 1u, 0u};
    assert(oc_details_pointer(&state, OC_DETAILS_CONFIRM, 1, 1) == OC_DETAILS_EFFECT_NONE);
    assert(oc_details_pointer(&state, OC_DETAILS_CANCEL, 0, 1) == OC_DETAILS_EFFECT_NONE);
    assert(state.confirming_delete); /* destructive drag-off is inert */
    assert(oc_details_pointer(&state, OC_DETAILS_CONFIRM, 1, 1) == OC_DETAILS_EFFECT_NONE);
    assert(oc_details_pointer(&state, OC_DETAILS_CONFIRM, 0, 1) == OC_DETAILS_EFFECT_DELETE);
    puts("PASS: confined details-modal input and launch policy");
    return 0;
}
