#ifndef OPENCONSOLE_DELETE_POLICY_H
#define OPENCONSOLE_DELETE_POLICY_H

#include <stdint.h>

typedef enum {
    OC_DELETE_REVALIDATE = 0,
    OC_DELETE_ENUMERATE,
    OC_DELETE_OPEN_CHILD,
    OC_DELETE_DELETE_CHILD,
    OC_DELETE_DELETE_ROOT,
    OC_DELETE_FLUSH,
    OC_DELETE_CLOSE,
    OC_DELETE_STAGE_COUNT
} oc_delete_stage;

typedef struct {
    uint32_t entries;
    uint32_t max_entries;
    uint32_t max_depth;
    uint8_t failed;
    uint8_t completed;
} oc_delete_guard;

int oc_delete_component_valid(const char *value);
int oc_delete_identity_valid(const char *directory, const char *manifest_id,
                             uint64_t expected_hash, uint64_t current_hash,
                             int same_volume, int active_launcher_alias);
void oc_delete_guard_init(oc_delete_guard *guard, uint32_t max_depth,
                          uint32_t max_entries);
int oc_delete_guard_visit(oc_delete_guard *guard, uint32_t depth);
int oc_delete_guard_stage(oc_delete_guard *guard, oc_delete_stage stage,
                          int firmware_success);

#endif
