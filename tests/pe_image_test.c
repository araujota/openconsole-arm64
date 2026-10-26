#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openconsole/pe_image.h"

static uint8_t *read_all(const char *path,size_t *size) {
    FILE *file=fopen(path,"rb");uint8_t *bytes;long length;
    assert(file);assert(fseek(file,0,SEEK_END)==0);length=ftell(file);assert(length>0);
    assert(fseek(file,0,SEEK_SET)==0);bytes=malloc((size_t)length);assert(bytes);
    assert(fread(bytes,1,(size_t)length,file)==(size_t)length);fclose(file);*size=(size_t)length;return bytes;
}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);}

int main(int argc,char **argv) {
    uint8_t *bytes,*copy;size_t size;uint32_t pe;uint16_t opt;oc_pe_info info;
    oc_pe_result result;
    assert(argc==2);bytes=read_all(argv[1],&size);
    result=oc_pe_validate_x64_efi(bytes,size,16u*1024u*1024u,OC_PE_ROLE_APPLICATION,&info);
    if(result!=OC_PE_OK)fprintf(stderr,"unexpected validation result: %s\n",oc_pe_result_string(result));
    assert(result==OC_PE_OK);
    assert(info.machine==OC_PE_MACHINE_X64&&info.subsystem==OC_PE_SUBSYSTEM_EFI_APPLICATION);
    assert(oc_pe_validate_x64_efi(bytes,size,1u,OC_PE_ROLE_APPLICATION,&info)==OC_PE_LIMIT);
    assert(oc_pe_validate_x64_efi(bytes,32u,size,OC_PE_ROLE_APPLICATION,&info)==OC_PE_TRUNCATED);
    copy=malloc(size);assert(copy);memcpy(copy,bytes,size);copy[0]='X';
    assert(oc_pe_validate_x64_efi(copy,size,size,OC_PE_ROLE_APPLICATION,&info)==OC_PE_BAD_DOS);
    memcpy(copy,bytes,size);pe=get32(copy+0x3cu);put16(copy+pe+4u,0x014cu);
    assert(oc_pe_validate_x64_efi(copy,size,size,OC_PE_ROLE_APPLICATION,&info)==OC_PE_BAD_MACHINE);
    memcpy(copy,bytes,size);opt=(uint16_t)(copy[pe+20u]|((uint16_t)copy[pe+21u]<<8u));
    (void)opt;put16(copy+pe+24u+68u,OC_PE_SUBSYSTEM_EFI_BOOT_DRIVER);
    assert(oc_pe_validate_x64_efi(copy,size,size,OC_PE_ROLE_DRIVER,&info)==OC_PE_OK);
    assert(oc_pe_validate_x64_efi(copy,size,size,OC_PE_ROLE_APPLICATION,&info)==OC_PE_BAD_SUBSYSTEM);
    free(copy);free(bytes);puts("PASS: bounded x64 PE32+ EFI application/driver validation");return 0;
}
