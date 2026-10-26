/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/ocg2.h"
#include <assert.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int verify(void*x,const uint8_t k[57],const uint8_t*m,size_t n,const uint8_t s[114]){EVP_PKEY*p=EVP_PKEY_new_raw_public_key(EVP_PKEY_ED448,0,k,57);EVP_MD_CTX*c=EVP_MD_CTX_new();int ok;(void)x;assert(p&&c);ok=EVP_DigestVerifyInit(c,0,0,0,p)==1&&EVP_DigestVerify(c,s,114,m,n)==1;EVP_MD_CTX_free(c);EVP_PKEY_free(p);return ok;}
static uint8_t*readall(const char*p,size_t*n){FILE*f=fopen(p,"rb");long z;uint8_t*b;assert(f);fseek(f,0,SEEK_END);z=ftell(f);rewind(f);b=malloc((size_t)z);assert(b);assert(fread(b,1,(size_t)z,f)==(size_t)z);fclose(f);*n=(size_t)z;return b;}
int main(int argc,char**argv){uint8_t*b,*k,*w,*bad;size_t n,kn;oc_ocg2_bundle g;oc_ocg2_entry e;oc_ocg2_result parsed;assert(argc==3);b=readall(argv[1],&n);k=readall(argv[2],&kn);assert(kn==57);parsed=oc_ocg2_parse(b,n,&g);if(parsed!=OC_OCG2_OK)fprintf(stderr,"%s\n",oc_ocg2_result_code(parsed));assert(parsed==OC_OCG2_OK);w=malloc(OC_OCG2_SIGNATURE_WORKSPACE);assert(w);assert(oc_ocg2_verify_signature(&g,k,verify,0,w,OC_OCG2_SIGNATURE_WORKSPACE)==OC_OCG2_OK);assert(oc_ocg2_find_role(&g,2,&e)==OC_OCG2_OK&&e.path_size==12);bad=malloc(n);memcpy(bad,b,n);bad[n-1]^=1;assert(oc_ocg2_parse(bad,n,&g)==OC_OCG2_FILE_DIGEST);memcpy(bad,b,n);bad[128]^=1;assert(oc_ocg2_parse(bad,n,&g)==OC_OCG2_OK);assert(oc_ocg2_verify_signature(&g,k,verify,0,w,OC_OCG2_SIGNATURE_WORKSPACE)==OC_OCG2_SIGNATURE);free(bad);free(w);free(k);free(b);puts("PASS: freestanding OCG2 structure, payload, and Ed448 verification");return 0;}
