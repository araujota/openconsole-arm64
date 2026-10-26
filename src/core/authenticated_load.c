/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/authenticated_load.h"
#include "openconsole/pe_image.h"
#include "openconsole/target.h"
static int text_equal(const char*a,const uint8_t*b,size_t n){size_t i=0;while(a[i])i++;if(i!=n)return 0;for(i=0;i<n;i++)if((uint8_t)a[i]!=b[i])return 0;return 1;}
static void copy(uint8_t*out,const uint8_t*in,size_t n){while(n--)*out++=*in++;}
oc_authenticated_load_result oc_authenticated_platform_image(const oc_platform_inventory*v,const char*path,uint16_t role,const uint8_t*image,size_t size,oc_pe_role pe_role){oc_platform_record r;oc_pe_info pe;if(!v||!path||!image)return OC_AUTH_LOAD_POLICY;if(oc_platform_inventory_find(v,path,role,&r)!=OC_PLATFORM_OK)return OC_AUTH_LOAD_NOT_AUTHORIZED;if(oc_platform_record_verify_bytes(&r,image,size)!=OC_PLATFORM_OK)return OC_AUTH_LOAD_DIGEST;if(oc_pe_validate_native_efi(image,size,64u*1024u*1024u,pe_role,&pe)!=OC_PE_OK)return OC_AUTH_LOAD_PE;return OC_AUTH_LOAD_OK;}
oc_authenticated_load_result oc_authenticated_game_bundle(
    const uint8_t*bytes,size_t size,const oc_publisher_key_store*keys,
    oc_ocg2_ed448_verify verify,void*context,uint8_t*workspace,
    size_t workspace_size,oc_authenticated_game*out) {
    oc_ocg2_bundle b;oc_ocg2_entry manifest,entry;oc_publisher_key key;
    oc_manifest_result parsed;oc_pe_info pe;oc_ocg2_result result;
    if(!bytes||!keys||!verify||!workspace||!out)return OC_AUTH_LOAD_POLICY;
    result=oc_ocg2_parse(bytes,size,&b);
    if(result!=OC_OCG2_OK)
        return result==OC_OCG2_FILE_DIGEST?OC_AUTH_LOAD_DIGEST:OC_AUTH_LOAD_POLICY;
    {
        oc_publisher_keys_result trust=
            oc_publisher_keys_find_game_signing(keys,b.publisher_key_id,&key);
        if(trust==OC_PUBLISHER_KEYS_REVOKED)return OC_AUTH_LOAD_REVOKED;
        if(trust!=OC_PUBLISHER_KEYS_OK)return OC_AUTH_LOAD_NOT_AUTHORIZED;
    }
    result=oc_ocg2_verify_signature(&b,key.public_key,verify,context,workspace,workspace_size);
    if(result!=OC_OCG2_OK)return result==OC_OCG2_LIMIT?OC_AUTH_LOAD_LIMIT:OC_AUTH_LOAD_SIGNATURE;
    if(oc_ocg2_find_role(&b,OC_OCG2_ROLE_MANIFEST,&manifest)!=OC_OCG2_OK)
        return OC_AUTH_LOAD_MANIFEST;
    parsed=oc_manifest_parse((const char*)manifest.data,(size_t)manifest.size,&out->manifest);
    if(parsed.code!=OC_MANIFEST_OK||
       oc_ocg2_find_role(&b,OC_TARGET_OCG2_ENTRY_ROLE_ID,&entry)!=OC_OCG2_OK||
       !text_equal(out->manifest.id,b.game_id,b.game_id_size)||
       !text_equal(out->manifest.version,b.version,b.version_size)||
       !text_equal(out->manifest.entry,entry.path,entry.path_size))return OC_AUTH_LOAD_MANIFEST;
    if(oc_pe_validate_native_efi(entry.data,(size_t)entry.size,32u*1024u*1024u,
                                 OC_PE_ROLE_APPLICATION,&pe)!=OC_PE_OK)return OC_AUTH_LOAD_PE;
    out->image=entry.data;out->image_size=(size_t)entry.size;
    copy(out->bundle_id,b.bundle_id,32);copy(out->publisher_key_id,b.publisher_key_id,32);
    return OC_AUTH_LOAD_OK;
}
const char*oc_authenticated_load_code(oc_authenticated_load_result r){static const char*const n[]={"OC_LOAD_OK","OC_LOAD_POLICY","OC_LOAD_NOT_AUTHORIZED","OC_LOAD_DIGEST","OC_LOAD_SIGNATURE","OC_LOAD_MANIFEST","OC_LOAD_PE","OC_LOAD_REVOKED","OC_LOAD_LIMIT"};return(unsigned)r<sizeof(n)/sizeof(n[0])?n[r]:"OC_LOAD_UNKNOWN";}
