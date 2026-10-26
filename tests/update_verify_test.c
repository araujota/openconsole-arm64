#include <assert.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/update_verify.h"

typedef struct { uint8_t id[16]; uint8_t public_key[OC_UPDATE_PUBLIC_KEY_BYTES]; } fixture;
static void w16(uint8_t *p, unsigned v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void w32(uint8_t *p, unsigned v) { unsigned i; for(i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
static void w64(uint8_t *p, uint64_t v) { unsigned i; for(i=0;i<8;++i)p[i]=(uint8_t)(v>>(8*i)); }
static int lookup(void *c, const uint8_t id[16], uint8_t key[OC_UPDATE_PUBLIC_KEY_BYTES]) {
    fixture *f=c; if (memcmp(f->id,id,16)!=0) return 0;
    memcpy(key,f->public_key,OC_UPDATE_PUBLIC_KEY_BYTES); return 1;
}
static int hash(void *c, const uint8_t *data, size_t size, uint8_t out[32]) {
    unsigned n=0; (void)c; return EVP_Digest(data,size,out,&n,EVP_sha256(),0)==1 && n==32;
}
static int verify(void *c, const uint8_t key[OC_UPDATE_PUBLIC_KEY_BYTES],
                  const uint8_t *message, size_t size,
                  const uint8_t signature[OC_UPDATE_SIGNATURE_BYTES]) {
    EVP_PKEY *pkey; EVP_MD_CTX *md; int ok; (void)c;
    pkey=EVP_PKEY_new_raw_public_key(EVP_PKEY_ED448,0,key,OC_UPDATE_PUBLIC_KEY_BYTES);
    if(!pkey)return 0;
    md=EVP_MD_CTX_new(); if(!md){EVP_PKEY_free(pkey);return 0;}
    ok=EVP_DigestVerifyInit(md,0,0,0,pkey)==1 &&
       EVP_DigestVerify(md,signature,OC_UPDATE_SIGNATURE_BYTES,message,size)==1;
    EVP_MD_CTX_free(md); EVP_PKEY_free(pkey); return ok;
}
static size_t make_metadata(uint8_t *blob, const uint8_t id[16],
                            const uint8_t digest[32]) {
    static const char url[]="https://updates.openconsole.test/bootx64.efi";
    size_t size=287+sizeof(url)-1; memset(blob,0,size); memcpy(blob,"OCU1",4);
    w16(blob+4,1); w16(blob+6,287); w32(blob+8,(unsigned)size);
    w64(blob+16,8); w64(blob+24,2000); w64(blob+32,12); w64(blob+40,4);
    memcpy(blob+48,id,16); memcpy(blob+137,digest,32); w16(blob+283,sizeof(url)-1);
    memcpy(blob+287,url,sizeof(url)-1); return size;
}
static void sign_metadata(uint8_t *blob, size_t size,
                          const uint8_t seed[OC_UPDATE_PUBLIC_KEY_BYTES]) {
    oc_update_metadata metadata; uint8_t signed_bytes[1024];
    size_t signed_size, sig_size=OC_UPDATE_SIGNATURE_BYTES;
    EVP_PKEY *pkey=EVP_PKEY_new_raw_private_key(EVP_PKEY_ED448,0,seed,
                                                OC_UPDATE_PUBLIC_KEY_BYTES);
    EVP_MD_CTX *md=EVP_MD_CTX_new(); assert(pkey && md);
    memset(blob+169,1,OC_UPDATE_SIGNATURE_BYTES);
    assert(oc_update_metadata_parse(blob,size,&metadata)==OC_METADATA_OK);
    assert(oc_update_write_signed(&metadata,signed_bytes,sizeof(signed_bytes),&signed_size)==OC_METADATA_OK);
    assert(EVP_DigestSignInit(md,0,0,0,pkey)==1);
    assert(EVP_DigestSign(md,blob+169,&sig_size,signed_bytes,signed_size)==1 &&
           sig_size==OC_UPDATE_SIGNATURE_BYTES);
    EVP_MD_CTX_free(md); EVP_PKEY_free(pkey);
}
int main(void) {
    static const uint8_t seed[OC_UPDATE_PUBLIC_KEY_BYTES]={1,2,3,4,5,6,7,8,9};
    static const uint8_t artifact[4]={0x4d,0x5a,0x90,0};
    uint8_t blob[1024], digest[32]; size_t size, key_size=OC_UPDATE_PUBLIC_KEY_BYTES;
    fixture f; oc_update_crypto crypto={&f,verify,hash,lookup};
    oc_https_identity identity={"updates.openconsole.test","updates.openconsole.test",1,1};
    oc_update_policy policy, accepted; oc_metadata_result policy_result;
    EVP_PKEY *pkey=EVP_PKEY_new_raw_private_key(EVP_PKEY_ED448,0,seed,
                                                OC_UPDATE_PUBLIC_KEY_BYTES);
    assert(pkey && EVP_PKEY_get_raw_public_key(pkey,f.public_key,&key_size)==1 &&
           key_size==OC_UPDATE_PUBLIC_KEY_BYTES);
    EVP_PKEY_free(pkey); memset(f.id,0x11,16); assert(hash(0,artifact,sizeof(artifact),digest));
    size=make_metadata(blob,f.id,digest); sign_metadata(blob,size,seed);
    memset(&policy,0,sizeof(policy)); memcpy(policy.trusted_key_id,f.id,16);
    policy.installed_version=10; policy.highest_sequence=7; policy.maximum_artifact_size=8192;
    assert(oc_update_verify(&crypto,&identity,&policy,1000,blob,size,artifact,sizeof(artifact),
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_OK);
    identity.verified_host="evil.openconsole.test";
    assert(oc_update_verify(&crypto,&identity,&policy,1000,blob,size,artifact,sizeof(artifact),
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_WRONG_HOST);
    identity.verified_host=identity.requested_host; identity.peer_chain_verified=0;
    assert(oc_update_verify(&crypto,&identity,&policy,1000,blob,size,artifact,sizeof(artifact),
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_TLS_REJECTED);
    identity.peer_chain_verified=1; blob[169]^=1;
    assert(oc_update_verify(&crypto,&identity,&policy,1000,blob,size,artifact,sizeof(artifact),
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_BAD_SIGNATURE);
    blob[169]^=1; blob[48]^=1;
    assert(oc_update_verify(&crypto,&identity,&policy,1000,blob,size,artifact,sizeof(artifact),
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_UNKNOWN_KEY);
    blob[48]^=1;
    assert(oc_update_verify(&crypto,&identity,&policy,1000,blob,size-1,artifact,sizeof(artifact),
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_INVALID);
    assert(oc_update_verify(&crypto,&identity,&policy,2001,blob,size,artifact,sizeof(artifact),
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_POLICY_REJECTED &&
           policy_result==OC_METADATA_EXPIRED);
    assert(oc_update_verify(&crypto,&identity,&policy,1000,blob,size,artifact,3,
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_ARTIFACT_SIZE);
    memset(&accepted,0xa5,sizeof(accepted)); blob[137]^=1; sign_metadata(blob,size,seed);
    assert(oc_update_verify(&crypto,&identity,&policy,1000,blob,size,artifact,sizeof(artifact),
                            &accepted,&policy_result)==OC_UPDATE_VERIFY_ARTIFACT_DIGEST);
    assert(accepted.highest_sequence == 0xa5a5a5a5a5a5a5a5ULL);
    puts("PASS: real Ed448 update verification and deterministic rejection matrix");
    return 0;
}
