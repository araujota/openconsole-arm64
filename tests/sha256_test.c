/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void hex(const char *s,uint8_t out[32]){size_t i;for(i=0;i<32;i++){unsigned v;assert(sscanf(s+i*2,"%2x",&v)==1);out[i]=(uint8_t)v;}}
static void check(const void *p,size_t n,const char *expected){uint8_t got[32],want[32];hex(expected,want);assert(oc_sha256(p,n,got));assert(memcmp(got,want,32)==0);}
int main(void){
    oc_sha256_context c;uint8_t got[32],want[32];
    check("",0,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("abc",3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    oc_sha256_init(&c);assert(oc_sha256_update(&c,"a",1));assert(oc_sha256_update(&c,"bc",2));oc_sha256_final(&c,got);
    hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",want);assert(memcmp(got,want,32)==0);
    puts("PASS: freestanding SHA-256 bootstrap implementation");return 0;
}
