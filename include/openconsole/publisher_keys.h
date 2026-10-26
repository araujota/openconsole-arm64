/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENCONSOLE_PUBLISHER_KEYS_H
#define OPENCONSOLE_PUBLISHER_KEYS_H
#include <stddef.h>
#include <stdint.h>
#define OC_PUBLISHER_KEY_MAX_RECORDS 256u
typedef enum {OC_PUBLISHER_KEYS_OK=0,OC_PUBLISHER_KEYS_TRUNCATED,OC_PUBLISHER_KEYS_MAGIC,OC_PUBLISHER_KEYS_VERSION,OC_PUBLISHER_KEYS_HEADER,OC_PUBLISHER_KEYS_DIGEST,OC_PUBLISHER_KEYS_RECORD,OC_PUBLISHER_KEYS_NOT_FOUND,OC_PUBLISHER_KEYS_REVOKED} oc_publisher_keys_result;
typedef struct {const uint8_t *blob;size_t size;uint32_t count;size_t offsets[OC_PUBLISHER_KEY_MAX_RECORDS];} oc_publisher_key_store;
typedef struct {const uint8_t *key_id;const uint8_t *public_key;const uint8_t *publisher_id;uint8_t publisher_id_size;const uint8_t *display_name;uint8_t display_name_size;uint8_t state;uint32_t scopes;} oc_publisher_key;
oc_publisher_keys_result oc_publisher_keys_parse(const uint8_t *,size_t,oc_publisher_key_store *);
oc_publisher_keys_result oc_publisher_keys_find_game_signing(const oc_publisher_key_store *,const uint8_t[32],oc_publisher_key *);
const char *oc_publisher_keys_result_code(oc_publisher_keys_result);
#endif
