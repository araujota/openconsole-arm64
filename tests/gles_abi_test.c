#include "openconsole/gles2_protocol.h"

#include <stddef.h>
#include <stdio.h>

_Static_assert(sizeof(OC_GLES_CONTEXT_DESC) == 32u, "context description ABI drift");
_Static_assert(sizeof(OC_GLES_CAPABILITIES) == 32u, "capabilities ABI drift");
_Static_assert(offsetof(OC_GLES2_PROTOCOL, query_capabilities) == 8u,
               "protocol function table ABI drift");
_Static_assert(sizeof(OC_GLES2_PROTOCOL) == 56u, "protocol size ABI drift");

int main(void) {
    if (OC_GLES_ABI_VERSION != 1u) return 1;
    puts("PASS: GLES bootstrap ABI layout");
    return 0;
}

