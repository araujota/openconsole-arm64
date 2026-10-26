/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/platform_inventory.h"
#include "openconsole/sha256.h"

static uint16_t u16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t u64(const uint8_t *p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static int range(size_t at,size_t n,size_t size){return at<=size&&n<=size-at;}
static int equal(const uint8_t *a,const uint8_t *b,size_t n){uint8_t d=0;while(n--)d|=*a++^*b++;return d==0;}
static uint8_t fold(uint8_t c){return c>='A'&&c<='Z'?(uint8_t)(c+('a'-'A')):c;}
static int folded_equal(const uint8_t *a,size_t an,const uint8_t *b,size_t bn){size_t i;if(an!=bn)return 0;for(i=0;i<an;i++)if(fold(a[i])!=fold(b[i]))return 0;return 1;}
static int role_valid(uint16_t r){return r==1u||(r>=10u&&r<=14u)||r==20u||r==30u||r==40u;}
static int version_char(uint8_t c,int first){return (c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(!first&&(c=='.'||c=='+'||c=='_'||c=='-'));}
static int path_valid(const uint8_t *p,size_t n){
    size_t i,start=0;int prefix;
    if(n==0||n>OC_PLATFORM_MAX_PATH||p[0]=='/')return 0;
    for(i=0;i<n;i++){uint8_t c=p[i];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='/'||c=='-'))return 0;if(c=='/'){if(i==start||(i-start==1&&p[start]=='.')||(i-start==2&&p[start]=='.'&&p[start+1]=='.'))return 0;start=i+1;}}
    if(start==n||(n-start==1&&p[start]=='.')||(n-start==2&&p[start]=='.'&&p[start+1]=='.'))return 0;
    prefix=n>=9&&fold(p[0])=='e'&&fold(p[1])=='f'&&fold(p[2])=='i'&&p[3]=='/'&&
        ((n>=9&&fold(p[4])=='b'&&fold(p[5])=='o'&&fold(p[6])=='o'&&fold(p[7])=='t'&&p[8]=='/')||
         (n>=16&&fold(p[4])=='o'&&fold(p[5])=='p'&&fold(p[6])=='e'&&fold(p[7])=='n'&&fold(p[8])=='c'&&fold(p[9])=='o'&&fold(p[10])=='n'&&fold(p[11])=='s'&&fold(p[12])=='o'&&fold(p[13])=='l'&&fold(p[14])=='e'&&p[15]=='/'));
    if(!prefix)return 0;
    {static const uint8_t self[]="EFI/OpenConsole/platform.ocp";if(folded_equal(p,n,self,sizeof(self)-1u))return 0;}
    return 1;
}
static int record_at(const oc_platform_inventory *v,size_t at,oc_platform_record *r,size_t *next){
    uint16_t pn,vn;if(!range(at,52u,v->index_size))return 0;pn=u16(v->index+at);vn=u16(v->index+at+48u);
    if(!range(at+52u,(size_t)pn+vn,v->index_size))return 0;
    r->path=v->index+at+52u;r->path_size=pn;r->version=r->path+pn;r->version_size=vn;
    r->role=u16(v->index+at+2u);r->flags=u32(v->index+at+4u);r->file_size=u64(v->index+at+8u);r->digest=v->index+at+16u;r->key_scope=u16(v->index+at+50u);*next=at+52u+pn+vn;return 1;
}

oc_platform_result oc_platform_inventory_parse(const uint8_t *b,size_t n,oc_platform_inventory *v){
    uint8_t digest[32];uint32_t ilen,count,plen,vlen,flags;uint64_t total,rbytes;size_t at,end,i,j;oc_platform_record r,prior;
    if(!b||!v||n<128u)return OC_PLATFORM_TRUNCATED;
    if(!equal(b,(const uint8_t *)"OCP1",4))return OC_PLATFORM_BAD_MAGIC;
    if(u16(b+4)!=1u||u16(b+6)!=0u)return OC_PLATFORM_UNSUPPORTED_VERSION;
    ilen=u32(b+12);count=u32(b+16);total=u64(b+24);
    if(u32(b+8)!=128u||ilen>OC_PLATFORM_MAX_INDEX||count==0u||count>OC_PLATFORM_MAX_ENTRIES||u16(b+20)!=1u||u16(b+22)!=114u)return OC_PLATFORM_BAD_HEADER;
    for(i=96;i<128;i++)if(b[i])return OC_PLATFORM_BAD_HEADER;
    if(total!=n||n!=128u+114u+(size_t)ilen)return OC_PLATFORM_LENGTH;
    if(!oc_sha256(b+242u,ilen,digest)||!equal(digest,b+32u,32u))return OC_PLATFORM_INDEX_DIGEST;
    v->blob=b;v->blob_size=n;v->signature=b+128u;v->index=b+242u;v->index_size=ilen;v->key_id=b+64u;v->inventory_id=b+32u;v->entry_count=count;
    if(ilen<64u||!equal(v->index,(const uint8_t *)"OPI1",4)||u16(v->index+4)!=1u||u16(v->index+6)!=0u||u32(v->index+8)!=64u||u32(v->index+12)!=count)return OC_PLATFORM_BAD_INDEX;
    plen=u32(v->index+16);vlen=u32(v->index+20);flags=u32(v->index+24);rbytes=u64(v->index+28);v->flags=flags;
    for(i=36;i<64;i++)if(v->index[i])return OC_PLATFORM_BAD_INDEX;
    if(flags&~1u||!range(64u,(size_t)plen+vlen,ilen))return OC_PLATFORM_BAD_INDEX;
    at=64u+plen+vlen;if(rbytes>SIZE_MAX||!range(at,(size_t)rbytes,ilen)||at+(size_t)rbytes!=ilen)return OC_PLATFORM_BAD_INDEX;end=ilen;
    for(i=0;i<count;i++){
        v->record_offsets[i]=at;if(!record_at(v,at,&r,&at))return OC_PLATFORM_BAD_INDEX;
        if(!path_valid(r.path,r.path_size)||r.version_size==0u||r.version_size>32u)return OC_PLATFORM_BAD_PATH;
        for(j=0;j<r.version_size;j++)if(!version_char(r.version[j],j==0u))return OC_PLATFORM_BAD_INDEX;
        if(!role_valid(r.role)||(r.flags&~1u)||r.key_scope<1u||r.key_scope>2u||r.file_size>64u*1024u*1024u)return OC_PLATFORM_BAD_ROLE;
        if(i){size_t k,minimum=r.path_size<prior.path_size?r.path_size:prior.path_size;int cmp=0;for(k=0;k<minimum;k++)if(r.path[k]!=prior.path[k]){cmp=r.path[k]>prior.path[k]?1:-1;break;}if(cmp==0)cmp=r.path_size>prior.path_size?1:(r.path_size<prior.path_size?-1:0);if(cmp<=0)return OC_PLATFORM_BAD_ORDER;}
        for(j=0;j<i;j++){size_t ignored;if(!record_at(v,v->record_offsets[j],&prior,&ignored))return OC_PLATFORM_BAD_INDEX;if(folded_equal(r.path,r.path_size,prior.path,prior.path_size))return OC_PLATFORM_BAD_ORDER;}
        prior=r;
    }
    return at==end?OC_PLATFORM_OK:OC_PLATFORM_BAD_INDEX;
}

oc_platform_result oc_platform_inventory_verify(const oc_platform_inventory *v,const uint8_t key[57],oc_platform_ed448_verify verify,void *context,uint8_t *work,size_t capacity){
    static const uint8_t domain[28]="OPENCONSOLE-PLATFORM-SIGNED";uint8_t kid[32];size_t need;
    if(!v||!key||!verify||!work)return OC_PLATFORM_BAD_SIGNATURE;
    if(!oc_sha256(key,57u,kid)||!equal(kid,v->key_id,32u))return OC_PLATFORM_UNTRUSTED_KEY;
    need=28u+128u+v->index_size;if(capacity<need)return OC_PLATFORM_BAD_SIGNATURE;
    {size_t i;for(i=0;i<28u;i++)work[i]=domain[i];for(i=0;i<128u;i++)work[28u+i]=v->blob[i];for(i=0;i<v->index_size;i++)work[156u+i]=v->index[i];}
    return verify(context,key,work,need,v->signature)?OC_PLATFORM_OK:OC_PLATFORM_BAD_SIGNATURE;
}

oc_platform_result oc_platform_inventory_find(const oc_platform_inventory *v,const char *path,uint16_t role,oc_platform_record *out){size_t n=0,i,next;if(!v||!path||!out)return OC_PLATFORM_NOT_FOUND;while(path[n])n++;for(i=0;i<v->entry_count;i++){if(!record_at(v,v->record_offsets[i],out,&next))return OC_PLATFORM_BAD_INDEX;if(out->role==role&&n==out->path_size&&equal((const uint8_t *)path,out->path,n))return OC_PLATFORM_OK;}return OC_PLATFORM_NOT_FOUND;}
oc_platform_result oc_platform_record_verify_bytes(const oc_platform_record *r,const uint8_t *b,size_t n){uint8_t d[32];if(!r||(!b&&n))return OC_PLATFORM_FILE_DIGEST;if(r->file_size!=n)return OC_PLATFORM_FILE_SIZE;if(!oc_sha256(b,n,d)||!equal(d,r->digest,32u))return OC_PLATFORM_FILE_DIGEST;return OC_PLATFORM_OK;}
const char *oc_platform_result_code(oc_platform_result r){static const char *const n[]={"OCP1_OK","OCP1_TRUNCATED","OCP1_BAD_MAGIC","OCP1_UNSUPPORTED_VERSION","OCP1_BAD_HEADER","OCP1_LENGTH_MISMATCH","OCP1_INDEX_DIGEST","OCP1_UNTRUSTED_KEY","OCP1_BAD_SIGNATURE","OCP1_BAD_INDEX","OCP1_BAD_PATH","OCP1_BAD_ORDER","OCP1_BAD_ROLE","OCP1_NOT_FOUND","OCP1_FILE_SIZE","OCP1_FILE_DIGEST"};return (unsigned)r<sizeof(n)/sizeof(n[0])?n[r]:"OCP1_UNKNOWN";}
