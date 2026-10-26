/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/platform_inventory.h"
#include <assert.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int verify(void *unused,const uint8_t key[57],const uint8_t *message,size_t size,const uint8_t signature[114]){
    EVP_PKEY *p;EVP_MD_CTX *c;int ok;(void)unused;p=EVP_PKEY_new_raw_public_key(EVP_PKEY_ED448,0,key,57);if(!p)return 0;c=EVP_MD_CTX_new();if(!c){EVP_PKEY_free(p);return 0;}ok=EVP_DigestVerifyInit(c,0,0,0,p)==1&&EVP_DigestVerify(c,signature,114,message,size)==1;EVP_MD_CTX_free(c);EVP_PKEY_free(p);return ok;
}
static uint8_t *readall(const char *path,size_t *size){FILE *f=fopen(path,"rb");long n;uint8_t *p;assert(f);assert(fseek(f,0,SEEK_END)==0);n=ftell(f);assert(n>=0);rewind(f);p=malloc((size_t)n+1u);assert(p);assert(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;}
int main(int argc,char **argv){
    uint8_t *blob,*key,*parent,*work,*bad;size_t blob_size,key_size,parent_size;oc_platform_inventory inventory;oc_platform_record record;
    assert(argc==4);blob=readall(argv[1],&blob_size);key=readall(argv[2],&key_size);parent=readall(argv[3],&parent_size);assert(key_size==57);
    assert(oc_platform_inventory_parse(blob,blob_size,&inventory)==OC_PLATFORM_OK);
    work=malloc(OC_PLATFORM_SIGNATURE_WORKSPACE);assert(work);
    assert(oc_platform_inventory_verify(&inventory,key,verify,0,work,OC_PLATFORM_SIGNATURE_WORKSPACE)==OC_PLATFORM_OK);
    assert(oc_platform_inventory_find(&inventory,"EFI/BOOT/BOOTX64.EFI",OC_PLATFORM_ROLE_PARENT,&record)==OC_PLATFORM_OK);
    assert(oc_platform_record_verify_bytes(&record,parent,parent_size)==OC_PLATFORM_OK);
    parent[0]^=1;assert(oc_platform_record_verify_bytes(&record,parent,parent_size)==OC_PLATFORM_FILE_DIGEST);parent[0]^=1;
    bad=malloc(blob_size);assert(bad);memcpy(bad,blob,blob_size);bad[242]^=1;assert(oc_platform_inventory_parse(bad,blob_size,&inventory)==OC_PLATFORM_INDEX_DIGEST);
    memcpy(bad,blob,blob_size);bad[128]^=1;assert(oc_platform_inventory_parse(bad,blob_size,&inventory)==OC_PLATFORM_OK);assert(oc_platform_inventory_verify(&inventory,key,verify,0,work,OC_PLATFORM_SIGNATURE_WORKSPACE)==OC_PLATFORM_BAD_SIGNATURE);
    assert(strcmp(oc_platform_result_code(OC_PLATFORM_FILE_DIGEST),"OCP1_FILE_DIGEST")==0);
    free(bad);free(work);free(parent);free(key);free(blob);puts("PASS: freestanding OCP1 parse, trust, lookup, and byte authorization");return 0;
}
