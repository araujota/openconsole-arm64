#ifndef OPENCONSOLE_UPDATE_METADATA_H
#define OPENCONSOLE_UPDATE_METADATA_H

#include <stddef.h>
#include <stdint.h>

#define OC_CATALOG_MAX_BYTES 65536u
#define OC_CATALOG_MAX_ENTRIES 32u
#define OC_CATALOG_MAX_ID 48u
#define OC_CATALOG_MAX_NAME 96u
#define OC_UPDATE_MAX_BYTES 4096u
#define OC_UPDATE_MAX_URL 512u
#define OC_UPDATE_KEY_ID_BYTES 16u
#define OC_UPDATE_PUBLIC_KEY_BYTES 57u
#define OC_UPDATE_DIGEST_BYTES 32u
#define OC_UPDATE_SIGNATURE_BYTES 114u
#define OC_UPDATE_FLAG_ROTATE_KEY 1u

typedef enum {
    OC_METADATA_OK = 0,
    OC_METADATA_INVALID,
    OC_METADATA_TOO_LARGE,
    OC_METADATA_NON_CANONICAL,
    OC_METADATA_EXPIRED,
    OC_METADATA_ROLLBACK,
    OC_METADATA_WRONG_KEY,
    OC_METADATA_ARTIFACT_TOO_LARGE
} oc_metadata_result;

typedef struct {
    const uint8_t *id;
    uint16_t id_length;
    const uint8_t *name;
    uint16_t name_length;
    const uint8_t *metadata_url;
    uint16_t metadata_url_length;
    uint64_t version;
    uint64_t artifact_size;
    uint8_t artifact_sha256[OC_UPDATE_DIGEST_BYTES];
} oc_catalog_entry;

typedef struct {
    uint64_t sequence;
    uint64_t expires_unix;
    uint16_t entry_count;
    oc_catalog_entry entries[OC_CATALOG_MAX_ENTRIES];
} oc_catalog;

typedef struct {
    uint16_t flags;
    uint64_t sequence;
    uint64_t expires_unix;
    uint64_t artifact_version;
    uint64_t artifact_size;
    uint8_t signing_key_id[OC_UPDATE_KEY_ID_BYTES];
    uint8_t next_key_id[OC_UPDATE_KEY_ID_BYTES];
    uint8_t next_public_key[OC_UPDATE_PUBLIC_KEY_BYTES];
    uint8_t artifact_sha256[OC_UPDATE_DIGEST_BYTES];
    uint8_t signature[OC_UPDATE_SIGNATURE_BYTES];
    const uint8_t *artifact_url;
    uint16_t artifact_url_length;
} oc_update_metadata;

typedef struct {
    uint64_t installed_version;
    uint64_t highest_sequence;
    uint64_t maximum_artifact_size;
    uint8_t trusted_key_id[OC_UPDATE_KEY_ID_BYTES];
    uint8_t pending_key_id[OC_UPDATE_KEY_ID_BYTES];
    uint8_t pending_public_key[OC_UPDATE_PUBLIC_KEY_BYTES];
    uint8_t has_pending_key;
} oc_update_policy;

oc_metadata_result oc_catalog_parse(const uint8_t *, size_t, oc_catalog *);
oc_metadata_result oc_update_metadata_parse(const uint8_t *, size_t,
                                            oc_update_metadata *);
size_t oc_update_signed_size(const oc_update_metadata *);
oc_metadata_result oc_update_write_signed(const oc_update_metadata *, uint8_t *,
                                          size_t, size_t *);
oc_metadata_result oc_update_policy_check(const oc_update_policy *,
                                          const oc_update_metadata *,
                                          uint64_t now_unix,
                                          oc_update_policy *accepted);

#endif
