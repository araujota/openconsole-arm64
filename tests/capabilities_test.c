/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/capabilities.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s\n", #x); return 1; } } while (0)

int main(void) {
    oc_host_capabilities host = {1u, 1u, 0u, 1u, 1u, 1u};
    oc_game_manifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    CHECK(oc_host_supports_capability(&host, "uefi.gop@1.0"));
    CHECK(oc_host_supports_capability(&host, "openconsole.audio@0.1"));
    CHECK(oc_host_supports_capability(&host, "openconsole.audio@0.2"));
    CHECK(!oc_host_supports_capability(&host, "openconsole.audio@0.3"));
    CHECK(!oc_host_supports_capability(&host, "openconsole.audio@1.0"));
    CHECK(!oc_host_supports_capability(&host, "uefi.input.pointer@1.0"));
    CHECK(!oc_host_supports_capability(&host, "unknown.feature@1.0"));
    strcpy(manifest.required_capabilities[0], "openconsole.net@0.1");
    strcpy(manifest.optional_capabilities[0], "uefi.input.pointer@1.0");
    manifest.required_capability_count = 1u;
    manifest.optional_capability_count = 1u;
    CHECK(oc_manifest_required_capabilities_supported(&manifest, &host));
    strcpy(manifest.required_capabilities[1], "uefi.input.pointer@1.0");
    manifest.required_capability_count = 2u;
    CHECK(!oc_manifest_required_capabilities_supported(&manifest, &host));
    puts("PASS: manifest-v2 capability negotiation and optional-capability policy");
    return 0;
}
