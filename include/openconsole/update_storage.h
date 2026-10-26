#ifndef OPENCONSOLE_UPDATE_STORAGE_H
#define OPENCONSOLE_UPDATE_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include "openconsole/update_metadata.h"

#define OC_UPDATE_SELECTOR_BYTES 176u
#define OC_UPDATE_PATH_MAX 64u

typedef enum {
    OC_UPDATE_STORAGE_OK = 0,
    OC_UPDATE_STORAGE_INVALID,
    OC_UPDATE_STORAGE_IO,
    OC_UPDATE_STORAGE_FLUSH,
    OC_UPDATE_STORAGE_VERIFY,
    OC_UPDATE_STORAGE_NO_BOOTABLE
} oc_update_storage_result;

typedef struct {
    void *context;
    int (*write_file)(void *, const char *, const uint8_t *, size_t);
    int (*flush_file)(void *, const char *);
    int (*read_file)(void *, const char *, uint8_t *, size_t, size_t *);
    int (*digest_file)(void *, const char *, uint64_t *, uint8_t[32]);
} oc_update_storage_ops;

typedef struct {
    uint64_t generation;
    uint64_t artifact_size;
    uint8_t artifact_sha256[32];
    uint8_t slot;
    uint8_t selector;
    oc_update_policy policy;
} oc_update_boot_selection;

const char *oc_update_artifact_path(uint8_t slot);
const char *oc_update_selector_path(uint8_t selector);

oc_update_storage_result oc_update_recover(const oc_update_storage_ops *,
                                           oc_update_boot_selection *);
oc_update_storage_result oc_update_stage_and_commit(
    const oc_update_storage_ops *, const uint8_t *, size_t,
    const uint8_t expected_sha256[32], const oc_update_policy *,
    oc_update_boot_selection *);

#endif
