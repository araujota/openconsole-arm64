/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENCONSOLE_CAPABILITIES_H
#define OPENCONSOLE_CAPABILITIES_H

#include <stdint.h>

#include "openconsole/manifest.h"

typedef struct {
    uint8_t gop;
    uint8_t keyboard;
    uint8_t pointer;
    uint8_t gles2;
    uint8_t network;
    uint8_t audio;
} oc_host_capabilities;

/* Returns nonzero only when the named capability is provided at a compatible
 * major version and at least the requested minor version. */
int oc_host_supports_capability(const oc_host_capabilities *host,
                                const char *requirement);

/* Optional capabilities never affect this result. */
int oc_manifest_required_capabilities_supported(
    const oc_game_manifest *manifest, const oc_host_capabilities *host);

#endif
