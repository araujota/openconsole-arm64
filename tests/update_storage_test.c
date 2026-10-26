#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/update_storage.h"

typedef struct { uint8_t data[256]; size_t size; int exists; } file;
typedef struct {
    file artifacts[2], selectors[2];
    int fail_write, fail_flush, corrupt_artifact, tear_selector;
} fixture;

static int path_index(const char *path, int selector) {
    int i;
    for (i = 0; i < 2; ++i) {
        const char *candidate = selector ? oc_update_selector_path((uint8_t)i) :
                                           oc_update_artifact_path((uint8_t)i);
        if (strcmp(path, candidate) == 0) return i;
    }
    return -1;
}
static void digest(const uint8_t *data, size_t size, uint8_t out[32]) {
    size_t i;
    memset(out, 0, 32);
    for (i = 0; i < size; ++i) {
        out[i % 32] ^= (uint8_t)(data[i] + (uint8_t)i);
        out[(i * 7u) % 32] ^= (uint8_t)(data[i] * 3u + 1u);
    }
}
static int write_file(void *context, const char *path, const uint8_t *data,
                      size_t size) {
    fixture *f = context; int index = path_index(path, 0); file *target;
    if (f->fail_write) return 0;
    target = index >= 0 ? &f->artifacts[index] : 0;
    if (target == 0) { index = path_index(path, 1); if (index >= 0) target = &f->selectors[index]; }
    if (target == 0 || size > sizeof(target->data)) return 0;
    if (f->tear_selector && path_index(path, 1) >= 0) size /= 2u;
    memcpy(target->data, data, size); target->size = size; target->exists = 1;
    return 1;
}
static int flush_file(void *context, const char *path) {
    fixture *f = context; (void)path;
    return !f->fail_flush;
}
static int read_file(void *context, const char *path, uint8_t *data,
                     size_t capacity, size_t *size) {
    fixture *f = context; int index = path_index(path, 0); file *source;
    source = index >= 0 ? &f->artifacts[index] : 0;
    if (source == 0) { index = path_index(path, 1); if (index >= 0) source = &f->selectors[index]; }
    if (source == 0 || !source->exists || source->size > capacity) return 0;
    memcpy(data, source->data, source->size); *size = source->size; return 1;
}
static int digest_file(void *context, const char *path, uint64_t *size,
                       uint8_t out[32]) {
    fixture *f = context; int index = path_index(path, 0);
    if (index < 0 || !f->artifacts[index].exists) return 0;
    *size = f->artifacts[index].size;
    digest(f->artifacts[index].data, f->artifacts[index].size, out);
    if (f->corrupt_artifact) out[0] ^= 1u;
    return 1;
}

int main(void) {
    fixture f; oc_update_storage_ops ops; oc_update_boot_selection selected, old;
    oc_update_policy policy;
    uint8_t first[] = {1,2,3,4}, second[] = {8,7,6,5,4}, hash[32];
    memset(&f, 0, sizeof(f));
    memset(&policy, 0, sizeof(policy));
    policy.installed_version=12u; policy.highest_sequence=8u;
    policy.maximum_artifact_size=8192u; memset(policy.trusted_key_id,0x11,16u);
    ops.context=&f; ops.write_file=write_file; ops.flush_file=flush_file;
    ops.read_file=read_file; ops.digest_file=digest_file;
    assert(oc_update_recover(&ops, &selected) == OC_UPDATE_STORAGE_NO_BOOTABLE);
    digest(first,sizeof(first),hash);
    assert(oc_update_stage_and_commit(&ops,first,sizeof(first),hash,&policy,&selected)==OC_UPDATE_STORAGE_OK);
    assert(selected.generation==1 && selected.slot==0 && selected.selector==0);
    assert(oc_update_recover(&ops,&old)==OC_UPDATE_STORAGE_OK && old.generation==1 &&
           old.policy.highest_sequence==8u && old.policy.installed_version==12u);
    policy.installed_version=13u; policy.highest_sequence=9u;
    digest(second,sizeof(second),hash);
    assert(oc_update_stage_and_commit(&ops,second,sizeof(second),hash,&policy,&selected)==OC_UPDATE_STORAGE_OK);
    assert(selected.generation==2 && selected.slot==1 && selected.selector==1);

    /* A corrupt selected artifact falls back to the retained prior slot. */
    f.artifacts[1].data[0] ^= 1u;
    assert(oc_update_recover(&ops,&old)==OC_UPDATE_STORAGE_OK && old.generation==1 && old.slot==0);
    f.artifacts[1].data[0] ^= 1u;

    /* A torn next selector cannot displace the last known-good generation. */
    digest(first,sizeof(first),hash);
    f.tear_selector=1;
    assert(oc_update_stage_and_commit(&ops,first,sizeof(first),hash,&policy,&old)==OC_UPDATE_STORAGE_VERIFY);
    f.tear_selector=0;
    assert(oc_update_recover(&ops,&old)==OC_UPDATE_STORAGE_OK && old.generation==2 && old.slot==1);

    f.fail_flush=1;
    assert(oc_update_stage_and_commit(&ops,first,sizeof(first),hash,&policy,&old)==OC_UPDATE_STORAGE_FLUSH);
    f.fail_flush=0; f.corrupt_artifact=1;
    assert(oc_update_stage_and_commit(&ops,first,sizeof(first),hash,&policy,&old)==OC_UPDATE_STORAGE_VERIFY);
    puts("PASS: two-slot staged update commit and last-known-good recovery");
    return 0;
}
