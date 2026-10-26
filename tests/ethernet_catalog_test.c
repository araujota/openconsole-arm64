#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openconsole/ethernet_catalog.h"

static uint8_t *read_all(const char *path,size_t *size){FILE *f=fopen(path,"rb");long n;uint8_t *p;assert(f);assert(!fseek(f,0,SEEK_END));n=ftell(f);assert(n>=0);assert(!fseek(f,0,SEEK_SET));p=malloc((size_t)n);assert(p);assert(fread(p,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));*size=(size_t)n;return p;}

int main(int argc,char **argv){uint8_t *blob,*bad;size_t size;oc_eth_catalog catalog;oc_eth_catalog_entry entry;assert(argc==2);blob=read_all(argv[1],&size);assert(oc_eth_catalog_parse(blob,size,&catalog)==OC_ETH_CATALOG_OK);assert(catalog.entry_count==1u);assert(oc_eth_catalog_entry_at(&catalog,0,&entry)==OC_ETH_CATALOG_OK);assert(entry.architecture==1u&&entry.vendor==0x10ecu&&entry.device==0x8125u);assert(entry.subsystem_vendor==0x1462u&&entry.subsystem_device==0x7e12u);assert(entry.revision_min==5u&&entry.revision_max==5u);assert(entry.protocols==(OC_ETH_PROTOCOL_NII|OC_ETH_PROTOCOL_UNDI));assert(entry.artifact_size==201616u);bad=malloc(size);assert(bad);memcpy(bad,blob,size);bad[0]='X';assert(oc_eth_catalog_parse(bad,size,&catalog)==OC_ETH_CATALOG_MAGIC);memcpy(bad,blob,size);bad[32+5]=1;assert(oc_eth_catalog_parse(bad,size,&catalog)==OC_ETH_CATALOG_ENTRY);assert(oc_eth_catalog_parse(blob,size-1u,&catalog)==OC_ETH_CATALOG_HEADER);free(bad);free(blob);puts("ethernet catalog tests passed");return 0;}
