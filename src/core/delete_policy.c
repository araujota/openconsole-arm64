#include "openconsole/delete_policy.h"

static int equal(const char *left, const char *right) {
    unsigned i = 0u;
    while (left[i] != '\0' && left[i] == right[i]) ++i;
    return left[i] == right[i];
}

int oc_delete_component_valid(const char *value) {
    unsigned i = 0u;
    if (value == 0 || value[0] == '\0' || equal(value, ".") || equal(value, ".."))
        return 0;
    while (value[i] != '\0') {
        unsigned char byte = (unsigned char)value[i++];
        if (i >= 128u || byte < 0x20u || byte >= 0x7fu ||
            byte == '/' || byte == '\\' || byte == ':') return 0;
    }
    return 1;
}

int oc_delete_identity_valid(const char *directory, const char *manifest_id,
                             uint64_t expected_hash, uint64_t current_hash,
                             int same_volume, int active_launcher_alias) {
    return oc_delete_component_valid(directory) &&
           oc_delete_component_valid(manifest_id) &&
           equal(directory, manifest_id) && expected_hash == current_hash &&
           same_volume && !active_launcher_alias;
}

void oc_delete_guard_init(oc_delete_guard *guard, uint32_t max_depth,
                          uint32_t max_entries) {
    guard->entries = 0u;
    guard->max_entries = max_entries;
    guard->max_depth = max_depth;
    guard->failed = 0u;
    guard->completed = 0u;
}

int oc_delete_guard_visit(oc_delete_guard *guard, uint32_t depth) {
    if (guard->failed || depth > guard->max_depth ||
        guard->entries >= guard->max_entries) {
        guard->failed = 1u;
        return 0;
    }
    ++guard->entries;
    return 1;
}

int oc_delete_guard_stage(oc_delete_guard *guard, oc_delete_stage stage,
                          int firmware_success) {
    if (guard->failed || stage >= OC_DELETE_STAGE_COUNT || !firmware_success) {
        guard->failed = 1u;
        guard->completed = 0u;
        return 0;
    }
    if (stage == OC_DELETE_CLOSE) guard->completed = 1u;
    return 1;
}
