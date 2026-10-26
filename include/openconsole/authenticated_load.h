/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENCONSOLE_AUTHENTICATED_LOAD_H
#define OPENCONSOLE_AUTHENTICATED_LOAD_H
#include "openconsole/manifest.h"
#include "openconsole/ocg2.h"
#include "openconsole/pe_image.h"
#include "openconsole/platform_inventory.h"
#include "openconsole/publisher_keys.h"
typedef enum {OC_AUTH_LOAD_OK=0,OC_AUTH_LOAD_POLICY,OC_AUTH_LOAD_NOT_AUTHORIZED,OC_AUTH_LOAD_DIGEST,OC_AUTH_LOAD_SIGNATURE,OC_AUTH_LOAD_MANIFEST,OC_AUTH_LOAD_PE,OC_AUTH_LOAD_REVOKED,OC_AUTH_LOAD_LIMIT} oc_authenticated_load_result;
typedef struct {const uint8_t*image;size_t image_size;oc_game_manifest manifest;uint8_t bundle_id[32];uint8_t publisher_key_id[32];} oc_authenticated_game;
oc_authenticated_load_result oc_authenticated_platform_image(const oc_platform_inventory*,const char*,uint16_t,const uint8_t*,size_t,oc_pe_role);
oc_authenticated_load_result oc_authenticated_game_bundle(const uint8_t*,size_t,const oc_publisher_key_store*,oc_ocg2_ed448_verify,void*,uint8_t*,size_t,oc_authenticated_game*);
const char*oc_authenticated_load_code(oc_authenticated_load_result);
#endif
