/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/authenticated_load.h"
#include <assert.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int verify(void*x,const uint8_t k[57],const uint8_t*m,size_t n,const uint8_t s[114]){EVP_PKEY*p=EVP_PKEY_new_raw_public_key(EVP_PKEY_ED448,0,k,57);EVP_MD_CTX*c=EVP_MD_CTX_new();int ok;(void)x;assert(p&&c);ok=EVP_DigestVerifyInit(c,0,0,0,p)==1&&EVP_DigestVerify(c,s,114,m,n)==1;EVP_MD_CTX_free(c);EVP_PKEY_free(p);return ok;}
static uint8_t*readall(const char*p,size_t*n){FILE*f=fopen(p,"rb");long z;uint8_t*b;assert(f);fseek(f,0,SEEK_END);z=ftell(f);rewind(f);b=malloc((size_t)z);assert(b);assert(fread(b,1,(size_t)z,f)==(size_t)z);fclose(f);*n=(size_t)z;return b;}
int main(int argc,char**argv){uint8_t*b,*store,*empty,*work;size_t n,sn,en;oc_publisher_key_store keys,no_keys;oc_authenticated_game game;assert(argc==4);b=readall(argv[1],&n);store=readall(argv[2],&sn);empty=readall(argv[3],&en);assert(oc_publisher_keys_parse(store,sn,&keys)==OC_PUBLISHER_KEYS_OK);assert(oc_publisher_keys_parse(empty,en,&no_keys)==OC_PUBLISHER_KEYS_OK);work=malloc(OC_OCG2_SIGNATURE_WORKSPACE);assert(work);assert(oc_authenticated_game_bundle(b,n,&keys,verify,0,work,OC_OCG2_SIGNATURE_WORKSPACE,&game)==OC_AUTH_LOAD_OK);assert(strcmp(game.manifest.id,"authenticated")==0&&game.image_size>0);assert(oc_authenticated_game_bundle(b,n,&no_keys,verify,0,work,OC_OCG2_SIGNATURE_WORKSPACE,&game)==OC_AUTH_LOAD_NOT_AUTHORIZED);b[n-1]^=1;assert(oc_authenticated_game_bundle(b,n,&keys,verify,0,work,OC_OCG2_SIGNATURE_WORKSPACE,&game)==OC_AUTH_LOAD_DIGEST);free(work);free(empty);free(store);free(b);puts("PASS: shared authenticated game load rejects untrusted and mutated OCG2 before PE load");return 0;}
