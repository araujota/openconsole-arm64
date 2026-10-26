/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/publisher_keys.h"
#include "openconsole/sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned char*readall(const char*p,size_t*n){FILE*f=fopen(p,"rb");long z;unsigned char*b;assert(f);fseek(f,0,SEEK_END);z=ftell(f);rewind(f);b=malloc((size_t)z);assert(b);assert(fread(b,1,(size_t)z,f)==(size_t)z);fclose(f);*n=(size_t)z;return b;}
int main(int argc,char**argv){unsigned char*b,*k,id[32];size_t n,kn;oc_publisher_key_store s;oc_publisher_key r;assert(argc==3);b=readall(argv[1],&n);k=readall(argv[2],&kn);assert(kn==57&&oc_sha256(k,kn,id));assert(oc_publisher_keys_parse(b,n,&s)==OC_PUBLISHER_KEYS_OK);assert(oc_publisher_keys_find_game_signing(&s,id,&r)==OC_PUBLISHER_KEYS_OK);assert(r.publisher_id_size==11);b[n-1]^=1;assert(oc_publisher_keys_parse(b,n,&s)==OC_PUBLISHER_KEYS_DIGEST);free(k);free(b);puts("PASS: freestanding OCPK trust lookup and tamper rejection");return 0;}
