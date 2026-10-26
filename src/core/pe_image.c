#include "openconsole/pe_image.h"

#define OC_PE_MAX_SECTIONS 96u
#define OC_PE_SECTION_EXECUTE 0x20000000u

static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8u); }
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);
}
static int range(size_t offset,size_t length,size_t size) {
    return offset<=size&&length<=size-offset;
}
static uint32_t maximum(uint32_t a,uint32_t b){return a>b?a:b;}
static int power_of_two(uint32_t value){return value!=0u&&(value&(value-1u))==0u;}

oc_pe_result oc_pe_validate_efi(const uint8_t *bytes,size_t size,
                                size_t maximum_size,oc_pe_role role,
                                uint16_t expected_machine,oc_pe_info *info) {
    uint32_t pe_offset,entry_rva,image_size,headers_size,section_alignment,file_alignment;
    uint32_t relocation_rva=0u,relocation_size=0u,directory_count;
    uint16_t sections,optional_size,subsystem;size_t optional,section_table,i;
    int entry_found=0,relocation_found=0;
    if(bytes==0||info==0||maximum_size==0u||role<OC_PE_ROLE_APPLICATION||
       role>OC_PE_ROLE_DRIVER)return OC_PE_BAD_OPTIONAL_HEADER;
    if(size>maximum_size)return OC_PE_LIMIT;
    if(!range(0u,64u,size))return OC_PE_TRUNCATED;
    if(bytes[0]!='M'||bytes[1]!='Z')return OC_PE_BAD_DOS;
    pe_offset=u32(bytes+0x3cu);
    if(!range(pe_offset,24u,size))return OC_PE_TRUNCATED;
    if(bytes[pe_offset]!='P'||bytes[pe_offset+1]!='E'||bytes[pe_offset+2]!=0u||
       bytes[pe_offset+3]!=0u)return OC_PE_BAD_SIGNATURE;
    if(expected_machine!=OC_PE_MACHINE_X64&&expected_machine!=OC_PE_MACHINE_ARM64)
        return OC_PE_BAD_MACHINE;
    if(u16(bytes+pe_offset+4u)!=expected_machine)return OC_PE_BAD_MACHINE;
    sections=u16(bytes+pe_offset+6u);optional_size=u16(bytes+pe_offset+20u);
    if(sections==0u||sections>OC_PE_MAX_SECTIONS)return OC_PE_LIMIT;
    optional=(size_t)pe_offset+24u;
    if(optional_size<112u||!range(optional,optional_size,size))return OC_PE_BAD_OPTIONAL_HEADER;
    if(u16(bytes+optional)!=0x20bu)return OC_PE_BAD_OPTIONAL_HEADER;
    entry_rva=u32(bytes+optional+16u);section_alignment=u32(bytes+optional+32u);
    file_alignment=u32(bytes+optional+36u);image_size=u32(bytes+optional+56u);
    headers_size=u32(bytes+optional+60u);subsystem=u16(bytes+optional+68u);
    if(role==OC_PE_ROLE_APPLICATION&&subsystem!=OC_PE_SUBSYSTEM_EFI_APPLICATION)
        return OC_PE_BAD_SUBSYSTEM;
    if(role==OC_PE_ROLE_DRIVER&&subsystem!=OC_PE_SUBSYSTEM_EFI_BOOT_DRIVER&&
       subsystem!=OC_PE_SUBSYSTEM_EFI_RUNTIME_DRIVER)return OC_PE_BAD_SUBSYSTEM;
    if(!power_of_two(section_alignment)||!power_of_two(file_alignment)||
       file_alignment>section_alignment||headers_size>size||image_size<headers_size)
        return OC_PE_BAD_ALIGNMENT;
    directory_count=u32(bytes+optional+108u);
    if(directory_count>16u)return OC_PE_BAD_OPTIONAL_HEADER;
    if(directory_count>5u) {
        if(optional_size<112u+6u*8u)return OC_PE_BAD_OPTIONAL_HEADER;
        relocation_rva=u32(bytes+optional+112u+5u*8u);
        relocation_size=u32(bytes+optional+116u+5u*8u);
    }
    if((relocation_rva==0u)!=(relocation_size==0u)||
       (relocation_rva!=0u&&relocation_size<8u))return OC_PE_BAD_RELOCATION;
    if(relocation_rva==0u)relocation_found=1;
    section_table=optional+optional_size;
    if(!range(section_table,(size_t)sections*40u,size))return OC_PE_TRUNCATED;
    for(i=0u;i<sections;++i) {
        const uint8_t *section=bytes+section_table+i*40u;
        uint32_t virtual_size=u32(section+8u),virtual_address=u32(section+12u);
        uint32_t raw_size=u32(section+16u),raw_offset=u32(section+20u);
        uint32_t characteristics=u32(section+36u),span=maximum(virtual_size,raw_size);
        if((raw_size!=0u&&!range(raw_offset,raw_size,size))||
           virtual_address>image_size||span>image_size-virtual_address)
            return OC_PE_BAD_SECTION;
        if(entry_rva>=virtual_address&&entry_rva-virtual_address<span&&
           (characteristics&OC_PE_SECTION_EXECUTE)!=0u)entry_found=1;
        if(relocation_rva>=virtual_address&&
           relocation_rva-virtual_address<span&&relocation_size<=span-(relocation_rva-virtual_address))
            relocation_found=1;
    }
    if(!entry_found)return OC_PE_BAD_ENTRY_POINT;
    if(!relocation_found)return OC_PE_BAD_RELOCATION;
    info->machine=expected_machine;info->subsystem=subsystem;info->section_count=sections;
    info->entry_rva=entry_rva;info->image_size=image_size;info->headers_size=headers_size;
    info->relocation_rva=relocation_rva;info->relocation_size=relocation_size;
    return OC_PE_OK;
}

oc_pe_result oc_pe_validate_x64_efi(const uint8_t *bytes,size_t size,
                                    size_t maximum_size,oc_pe_role role,
                                    oc_pe_info *info) {
    return oc_pe_validate_efi(bytes,size,maximum_size,role,OC_PE_MACHINE_X64,info);
}

oc_pe_result oc_pe_validate_native_efi(const uint8_t *bytes,size_t size,
                                       size_t maximum_size,oc_pe_role role,
                                       oc_pe_info *info) {
    return oc_pe_validate_efi(bytes,size,maximum_size,role,OC_TARGET_PE_MACHINE,info);
}

const char *oc_pe_result_string(oc_pe_result result) {
    static const char *const names[]={"ok","truncated","bad DOS header","bad PE signature",
        "wrong machine","bad optional header","wrong EFI subsystem","bad alignment",
        "bad section","bad entry point","bad relocation directory","size limit"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown";
}
