#include "openconsole/ethernet_catalog.h"

#define HEADER_SIZE 32u
#define ENTRY_SIZE 192u

static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t u64(const uint8_t *p) { uint64_t v = 0; unsigned i; for (i=0;i<8;i++) v |= (uint64_t)p[i] << (8*i); return v; }
static int eq(const uint8_t *a,const char *b,size_t n){while(n--)if(*a++!=(uint8_t)*b++)return 0;return 1;}
static int ident(const uint8_t *p,size_t n){size_t i;if(!n||n>=OC_ETH_CATALOG_ID_BYTES)return 0;for(i=0;i<n;i++)if(!((p[i]>='a'&&p[i]<='z')||(p[i]>='0'&&p[i]<='9')||p[i]=='.'||p[i]=='_'||p[i]=='-'))return 0;return 1;}
static int path(const uint8_t *p,size_t n){static const char prefix[]="EFI/OpenConsole/Drivers/Network/";size_t i;if(n<=sizeof(prefix)-1u||n>OC_ETH_CATALOG_PATH_BYTES||!eq(p,prefix,sizeof(prefix)-1u))return 0;for(i=0;i<n;i++)if(!((p[i]>='A'&&p[i]<='Z')||(p[i]>='a'&&p[i]<='z')||(p[i]>='0'&&p[i]<='9')||p[i]=='.'||p[i]=='_'||p[i]=='/'||p[i]=='-'))return 0;return p[n-4]=='.'&&p[n-3]=='e'&&p[n-2]=='f'&&p[n-1]=='i';}
static int id_order(const oc_eth_catalog_entry *a,const oc_eth_catalog_entry *b){size_t i,n=a->manifest_id_size<b->manifest_id_size?a->manifest_id_size:b->manifest_id_size;for(i=0;i<n;i++){if(a->manifest_id[i]<b->manifest_id[i])return -1;if(a->manifest_id[i]>b->manifest_id[i])return 1;}return a->manifest_id_size<b->manifest_id_size?-1:a->manifest_id_size>b->manifest_id_size?1:0;}

oc_eth_catalog_result oc_eth_catalog_entry_at(const oc_eth_catalog *c,uint32_t index,oc_eth_catalog_entry *e){
    const uint8_t *p;size_t i;if(!c||!e||index>=c->entry_count)return OC_ETH_CATALOG_ENTRY;p=c->blob+HEADER_SIZE+(size_t)index*ENTRY_SIZE;
    e->path_size=u16(p);e->manifest_id_size=u16(p+2);e->architecture=p[4];e->protocols=u16(p+6);e->vendor=u16(p+8);e->device=u16(p+10);e->subsystem_vendor=u16(p+12);e->subsystem_device=u16(p+14);e->revision_min=p[16];e->revision_max=p[17];e->priority=u32(p+20);e->artifact_size=u64(p+24);e->sha256=p+32;e->path=p+64;e->manifest_id=p+160;
    if(p[5]||u16(p+18)||!e->protocols||(e->protocols&~(OC_ETH_PROTOCOL_NII|OC_ETH_PROTOCOL_UNDI|OC_ETH_PROTOCOL_SNP))||e->architecture<1u||e->architecture>2u||!e->vendor||!e->device||e->revision_min>e->revision_max||!e->artifact_size||e->artifact_size>4u*1024u*1024u||!path(e->path,e->path_size)||!ident(e->manifest_id,e->manifest_id_size))return OC_ETH_CATALOG_ENTRY;
    for(i=e->path_size;i<OC_ETH_CATALOG_PATH_BYTES;i++)if(e->path[i])return OC_ETH_CATALOG_ENTRY;
    for(i=e->manifest_id_size;i<OC_ETH_CATALOG_ID_BYTES;i++)if(e->manifest_id[i])return OC_ETH_CATALOG_ENTRY;
    return OC_ETH_CATALOG_OK;
}

oc_eth_catalog_result oc_eth_catalog_parse(const uint8_t *b,size_t n,oc_eth_catalog *c){uint32_t count,i;uint64_t total;oc_eth_catalog_entry e,prior;if(!b||!c||n<HEADER_SIZE)return OC_ETH_CATALOG_TRUNCATED;if(!eq(b,"OEC1",4))return OC_ETH_CATALOG_MAGIC;if(u16(b+4)!=1u||u16(b+6)!=0u)return OC_ETH_CATALOG_VERSION;count=u32(b+16);total=u64(b+24);if(u32(b+8)!=HEADER_SIZE||u32(b+12)!=ENTRY_SIZE||count>OC_ETH_CATALOG_MAX_ENTRIES||u32(b+20)||total!=n||n!=HEADER_SIZE+(size_t)count*ENTRY_SIZE)return OC_ETH_CATALOG_HEADER;c->blob=b;c->blob_size=n;c->entry_count=count;for(i=0;i<count;i++){if(oc_eth_catalog_entry_at(c,i,&e)!=OC_ETH_CATALOG_OK)return OC_ETH_CATALOG_ENTRY;if(i&&(e.priority<prior.priority||(e.priority==prior.priority&&id_order(&prior,&e)>=0)))return OC_ETH_CATALOG_ENTRY;prior=e;}return OC_ETH_CATALOG_OK;}

const char *oc_eth_catalog_result_code(oc_eth_catalog_result r){static const char*const n[]={"OEC1_OK","OEC1_TRUNCATED","OEC1_BAD_MAGIC","OEC1_UNSUPPORTED_VERSION","OEC1_BAD_HEADER","OEC1_BAD_ENTRY"};return(unsigned)r<sizeof(n)/sizeof(n[0])?n[r]:"OEC1_UNKNOWN";}
