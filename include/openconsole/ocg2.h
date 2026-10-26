/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENCONSOLE_OCG2_H
#define OPENCONSOLE_OCG2_H
#include <stddef.h>
#include <stdint.h>
#define OC_OCG2_MAX_ENTRIES 256u
#define OC_OCG2_SIGNATURE_WORKSPACE (24u+128u+256u*1024u)
#define OC_OCG2_ROLE_MANIFEST 1u
#define OC_OCG2_ROLE_ENTRY_X64 2u
#define OC_OCG2_ROLE_JACKET_OCJ1 3u
#define OC_OCG2_ROLE_CONTROLS_V1 4u
#define OC_OCG2_ROLE_DATA 5u
#define OC_OCG2_ROLE_ENTRY_ARM64 6u
typedef enum {OC_OCG2_OK=0,OC_OCG2_LIMIT,OC_OCG2_MAGIC,OC_OCG2_VERSION,OC_OCG2_HEADER,OC_OCG2_LENGTH,OC_OCG2_INDEX_DIGEST,OC_OCG2_UNTRUSTED_KEY,OC_OCG2_SIGNATURE,OC_OCG2_INDEX,OC_OCG2_PATH,OC_OCG2_ORDER,OC_OCG2_ROLE,OC_OCG2_OFFSET,OC_OCG2_FILE_DIGEST,OC_OCG2_NOT_FOUND,OC_OCG2_MANIFEST} oc_ocg2_result;
typedef struct {const uint8_t*path;uint16_t path_size;uint16_t role;uint64_t offset;uint64_t size;const uint8_t*digest;const uint8_t*data;} oc_ocg2_entry;
typedef struct {const uint8_t*blob;size_t size;const uint8_t*signature;const uint8_t*index;size_t index_size;const uint8_t*payload;size_t payload_size;const uint8_t*bundle_id;const uint8_t*publisher_key_id;const uint8_t*game_id;uint32_t game_id_size;const uint8_t*version;uint32_t version_size;uint32_t count;uint32_t manifest_index;size_t offsets[OC_OCG2_MAX_ENTRIES];} oc_ocg2_bundle;
typedef int(*oc_ocg2_ed448_verify)(void*,const uint8_t[57],const uint8_t*,size_t,const uint8_t[114]);
oc_ocg2_result oc_ocg2_parse(const uint8_t*,size_t,oc_ocg2_bundle*);
oc_ocg2_result oc_ocg2_verify_signature(const oc_ocg2_bundle*,const uint8_t[57],oc_ocg2_ed448_verify,void*,uint8_t*,size_t);
oc_ocg2_result oc_ocg2_find_role(const oc_ocg2_bundle*,uint16_t,oc_ocg2_entry*);
oc_ocg2_result oc_ocg2_find_path(const oc_ocg2_bundle*,const char*,uint16_t,oc_ocg2_entry*);
oc_ocg2_result oc_ocg2_entry_at(const oc_ocg2_bundle*,uint32_t,oc_ocg2_entry*);
const char*oc_ocg2_result_code(oc_ocg2_result);
#endif
