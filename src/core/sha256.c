/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/sha256.h"

static const uint32_t constants[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static uint32_t rotate(uint32_t value,unsigned count) { return (value>>count)|(value<<(32u-count)); }
static uint32_t load32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void store32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }

static void transform(oc_sha256_context *c,const uint8_t block[64]) {
    uint32_t w[64],a,b,d,e,f,g,h,i,j,t1,t2;
    for(i=0;i<16u;++i)w[i]=load32(block+i*4u);
    for(i=16u;i<64u;++i) {
        uint32_t s0=rotate(w[i-15u],7)^rotate(w[i-15u],18)^(w[i-15u]>>3);
        uint32_t s1=rotate(w[i-2u],17)^rotate(w[i-2u],19)^(w[i-2u]>>10);
        w[i]=w[i-16u]+s0+w[i-7u]+s1;
    }
    a=c->state[0];b=c->state[1];d=c->state[2];e=c->state[3];
    f=c->state[4];g=c->state[5];h=c->state[6];i=c->state[7];
    for(j=0;j<64u;++j) {
        uint32_t s1=rotate(f,6)^rotate(f,11)^rotate(f,25);
        uint32_t choose=(f&g)^((~f)&h);
        t1=i+s1+choose+constants[j]+w[j];
        t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&d)^(b&d));
        i=h;h=g;g=f;f=e+t1;e=d;d=b;b=a;a=t1+t2;
    }
    c->state[0]+=a;c->state[1]+=b;c->state[2]+=d;c->state[3]+=e;
    c->state[4]+=f;c->state[5]+=g;c->state[6]+=h;c->state[7]+=i;
}

void oc_sha256_init(oc_sha256_context *c) {
    static const uint32_t initial[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    size_t n; if(c==0)return;for(n=0;n<8u;++n)c->state[n]=initial[n];c->bytes=0;c->used=0;
}

int oc_sha256_update(oc_sha256_context *c,const void *input,size_t size) {
    const uint8_t *p=input;size_t take,n;
    if(c==0||(size!=0u&&p==0)||c->bytes>UINT64_MAX-size)return 0;
    c->bytes+=(uint64_t)size;
    while(size!=0u){take=64u-c->used;if(take>size)take=size;for(n=0;n<take;++n)c->block[c->used+n]=p[n];c->used+=take;p+=take;size-=take;if(c->used==64u){transform(c,c->block);c->used=0u;}}
    return 1;
}

void oc_sha256_final(oc_sha256_context *c,uint8_t out[32]) {
    uint64_t bits;size_t n;if(c==0||out==0)return;bits=c->bytes*8u;c->block[c->used++]=0x80u;
    if(c->used>56u){while(c->used<64u)c->block[c->used++]=0u;transform(c,c->block);c->used=0u;}
    while(c->used<56u)c->block[c->used++]=0u;
    for(n=0;n<8u;++n)c->block[63u-n]=(uint8_t)(bits>>(n*8u));
    transform(c,c->block);
    for(n=0;n<8u;++n)store32(out+n*4u,c->state[n]);
    for(n=0;n<sizeof(*c);++n)((uint8_t *)c)[n]=0u;
}

int oc_sha256(const void *input,size_t size,uint8_t out[32]) { oc_sha256_context c;if(out==0)return 0;oc_sha256_init(&c);if(!oc_sha256_update(&c,input,size))return 0;oc_sha256_final(&c,out);return 1; }
