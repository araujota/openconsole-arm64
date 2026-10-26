#ifndef OPENCONSOLE_GAME_STORAGE_H
#define OPENCONSOLE_GAME_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#define OC_GAME_SELECTION_BYTES 196u
#define OC_GAME_ID_MAX 64u
#define OC_GAME_VERSION_MAX 32u
#define OC_GAME_STORAGE_PATH_MAX 192u
#define OC_GAME_BUNDLE_MAX (64u * 1024u * 1024u)
#define OC_GAME_INVENTORY_MAX (256u * 1024u)
#define OC_GAME_GC_MAX_DELETES 32u
#define OC_GAME_INSTALL_MAX_FILES 256u

typedef enum {
    OC_GAME_STORAGE_OK = 0,
    OC_GAME_STORAGE_INVALID,
    OC_GAME_STORAGE_NO_SELECTION,
    OC_GAME_STORAGE_OVERFLOW,
    OC_GAME_STORAGE_IO,
    OC_GAME_STORAGE_FLUSH,
    OC_GAME_STORAGE_VERIFY,
    OC_GAME_STORAGE_CLEANUP
} oc_game_storage_result;

typedef enum {
    OC_GAME_PATH_BUNDLE = 1,
    OC_GAME_PATH_DESCRIPTOR,
    OC_GAME_PATH_STAGING,
    OC_GAME_PATH_SELECTOR,
    OC_GAME_PATH_SLOT
} oc_game_path_kind;

typedef struct {
    const char *path;
    const uint8_t *data;
    size_t size;
    const uint8_t *sha256;
} oc_game_install_file;

typedef struct {
    uint64_t generation;
    uint64_t bundle_size;
    uint8_t bundle_id[32];
    uint8_t inventory_digest[32];
    char game_id[OC_GAME_ID_MAX + 1u];
    char version[OC_GAME_VERSION_MAX + 1u];
    uint8_t slot;
    uint8_t selector;
    uint8_t tombstone;
} oc_game_selection;

typedef int (*oc_game_slot_validator)(void *context,
                                      const oc_game_selection *candidate);

/* digest_file MUST reopen the named path and digest the persisted bytes. */
typedef struct {
    void *context;
    int (*write_file)(void *, const char *, const uint8_t *, size_t);
    int (*flush_file)(void *, const char *);
    /* Returns 1 on success, 0 when absent, and -1 on I/O/reopen failure. */
    int (*read_file)(void *, const char *, uint8_t *, size_t, size_t *);
    int (*digest_file)(void *, const char *, uint64_t *, uint8_t[32]);
    int (*delete_file)(void *, const char *); /* Missing files are success. */
    int (*cleanup_slot)(void *, const char *, size_t, size_t *, int *);
    int (*validate_slot)(void *, const char *, const char *,
                         const oc_game_selection *);
} oc_game_storage_ops;

oc_game_storage_result oc_game_selection_encode(
    const oc_game_selection *, uint8_t out[OC_GAME_SELECTION_BYTES]);
oc_game_storage_result oc_game_selection_decode(
    const uint8_t in[OC_GAME_SELECTION_BYTES], uint8_t selector,
    oc_game_selection *);
oc_game_storage_result oc_game_selection_recover(
    const uint8_t *record_a, size_t record_a_size,
    const uint8_t *record_b, size_t record_b_size,
    oc_game_slot_validator, void *validator_context,
    oc_game_selection *selected);
oc_game_storage_result oc_game_selection_next(
    const oc_game_selection *current, const char *game_id, const char *version,
    const uint8_t bundle_id[32], const uint8_t inventory_digest[32],
    uint64_t bundle_size, int tombstone, oc_game_selection *next);

oc_game_storage_result oc_game_storage_path(
    const char *game_id, oc_game_path_kind, uint8_t index,
    char *out, size_t out_capacity);
oc_game_storage_result oc_game_package_path(
    const char *game_id, uint8_t slot, const char *relative_path,
    char *out, size_t out_capacity);

oc_game_storage_result oc_game_transaction_recover(
    const oc_game_storage_ops *, const char *game_id,
    size_t maximum_cleanup_deletes, size_t *cleanup_deletes,
    oc_game_selection *selected);

oc_game_storage_result oc_game_stage_and_commit(
    const oc_game_storage_ops *, const char *game_id, const char *version,
    const uint8_t *bundle, size_t bundle_size,
    const uint8_t bundle_sha256[32], const uint8_t bundle_id[32],
    const uint8_t *descriptor, size_t descriptor_size,
    const uint8_t descriptor_sha256[32],
    const oc_game_install_file *files, size_t file_count,
    oc_game_selection *committed);

oc_game_storage_result oc_game_tombstone_and_cleanup(
    const oc_game_storage_ops *, const char *game_id, int confirmed,
    size_t maximum_cleanup_deletes, size_t *cleanup_deletes,
    oc_game_selection *committed);

#endif
