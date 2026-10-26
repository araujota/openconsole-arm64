/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/capabilities.h"

#include <stddef.h>

typedef struct {
    const char *name;
    uint32_t major;
    uint32_t minor;
    uint8_t available;
} capability;

static int name_equal(const char *text, size_t size, const char *expected) {
    size_t i = 0u;
    while (i < size && expected[i] != '\0' && text[i] == expected[i]) ++i;
    return i == size && expected[i] == '\0';
}

static int parse_requirement(const char *text, const char **name, size_t *name_size,
                             uint32_t *major, uint32_t *minor) {
    size_t at = 0u;
    uint32_t first = 0u, second = 0u;
    if (text == 0 || text[0] == '\0') return 0;
    while (text[at] != '\0' && text[at] != '@') ++at;
    if (at == 0u || text[at] != '@') return 0;
    *name = text;
    *name_size = at++;
    if (text[at] < '0' || text[at] > '9') return 0;
    while (text[at] >= '0' && text[at] <= '9') {
        uint32_t digit = (uint32_t)(text[at++] - '0');
        if (first > (UINT32_MAX - digit) / 10u) return 0;
        first = first * 10u + digit;
    }
    if (text[at++] != '.' || text[at] < '0' || text[at] > '9') return 0;
    while (text[at] >= '0' && text[at] <= '9') {
        uint32_t digit = (uint32_t)(text[at++] - '0');
        if (second > (UINT32_MAX - digit) / 10u) return 0;
        second = second * 10u + digit;
    }
    if (text[at] != '\0') return 0;
    *major = first;
    *minor = second;
    return 1;
}

int oc_host_supports_capability(const oc_host_capabilities *host,
                                const char *requirement) {
    capability registry[6];
    const char *name;
    size_t name_size, i;
    uint32_t major, minor;
    if (host == 0 || !parse_requirement(requirement, &name, &name_size,
                                         &major, &minor)) return 0;
    registry[0] = (capability){"uefi.gop", 1u, 0u, host->gop};
    registry[1] = (capability){"uefi.input.keyboard", 1u, 0u, host->keyboard};
    registry[2] = (capability){"uefi.input.pointer", 1u, 0u, host->pointer};
    registry[3] = (capability){"openconsole.gles2", 0u, 1u, host->gles2};
    registry[4] = (capability){"openconsole.net", 0u, 1u, host->network};
    registry[5] = (capability){"openconsole.audio", 0u, 2u, host->audio};
    for (i = 0u; i < sizeof(registry) / sizeof(registry[0]); ++i) {
        if (name_equal(name, name_size, registry[i].name))
            return registry[i].available && major == registry[i].major &&
                   minor <= registry[i].minor;
    }
    return 0;
}

int oc_manifest_required_capabilities_supported(
    const oc_game_manifest *manifest, const oc_host_capabilities *host) {
    uint32_t i;
    if (manifest == 0 || host == 0) return 0;
    for (i = 0u; i < manifest->required_capability_count; ++i)
        if (!oc_host_supports_capability(host, manifest->required_capabilities[i]))
            return 0;
    return 1;
}
