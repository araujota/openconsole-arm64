/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENCONSOLE_PLATFORM_INVENTORY_H
#define OPENCONSOLE_PLATFORM_INVENTORY_H

#include <stddef.h>
#include <stdint.h>

#define OC_PLATFORM_MAX_INDEX (256u * 1024u)
#define OC_PLATFORM_MAX_ENTRIES 256u
#define OC_PLATFORM_MAX_PATH 240u
#define OC_PLATFORM_SIGNATURE_BYTES 114u
#define OC_PLATFORM_PUBLIC_KEY_BYTES 57u
#define OC_PLATFORM_SIGNATURE_WORKSPACE (28u + 128u + OC_PLATFORM_MAX_INDEX)

typedef enum {
    OC_PLATFORM_OK=0, OC_PLATFORM_TRUNCATED, OC_PLATFORM_BAD_MAGIC,
    OC_PLATFORM_UNSUPPORTED_VERSION, OC_PLATFORM_BAD_HEADER,
    OC_PLATFORM_LENGTH, OC_PLATFORM_INDEX_DIGEST, OC_PLATFORM_UNTRUSTED_KEY,
    OC_PLATFORM_BAD_SIGNATURE, OC_PLATFORM_BAD_INDEX, OC_PLATFORM_BAD_PATH,
    OC_PLATFORM_BAD_ORDER, OC_PLATFORM_BAD_ROLE, OC_PLATFORM_NOT_FOUND,
    OC_PLATFORM_FILE_SIZE, OC_PLATFORM_FILE_DIGEST
} oc_platform_result;

typedef enum {
    OC_PLATFORM_ROLE_PARENT=1, OC_PLATFORM_ROLE_INPUT=10,
    OC_PLATFORM_ROLE_NETWORK=11, OC_PLATFORM_ROLE_AUDIO=12,
    OC_PLATFORM_ROLE_CRYPTO=13, OC_PLATFORM_ROLE_GRAPHICS=14,
    OC_PLATFORM_ROLE_TRUST=20, OC_PLATFORM_ROLE_NOTICES=30,
    OC_PLATFORM_ROLE_RECOVERY=40
} oc_platform_role;

typedef struct {
    const uint8_t *path;
    uint16_t path_size;
    const uint8_t *version;
    uint16_t version_size;
    uint16_t role;
    uint16_t key_scope;
    uint32_t flags;
    uint64_t file_size;
    const uint8_t *digest;
} oc_platform_record;

typedef struct {
    const uint8_t *blob;
    size_t blob_size;
    const uint8_t *signature;
    const uint8_t *index;
    size_t index_size;
    const uint8_t *key_id;
    const uint8_t *inventory_id;
    uint32_t flags;
    uint32_t entry_count;
    size_t record_offsets[OC_PLATFORM_MAX_ENTRIES];
} oc_platform_inventory;

typedef int (*oc_platform_ed448_verify)(void *,const uint8_t[57],
    const uint8_t *,size_t,const uint8_t[114]);

oc_platform_result oc_platform_inventory_parse(const uint8_t *,size_t,
                                                oc_platform_inventory *);
oc_platform_result oc_platform_inventory_verify(
    const oc_platform_inventory *,const uint8_t public_key[57],
    oc_platform_ed448_verify,void *,uint8_t *,size_t);
oc_platform_result oc_platform_inventory_find(
    const oc_platform_inventory *,const char *,uint16_t,oc_platform_record *);
oc_platform_result oc_platform_record_verify_bytes(
    const oc_platform_record *,const uint8_t *,size_t);
const char *oc_platform_result_code(oc_platform_result);

#endif
