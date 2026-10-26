#ifndef OPENCONSOLE_PE_IMAGE_H
#define OPENCONSOLE_PE_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include "openconsole/target.h"

#define OC_PE_SUBSYSTEM_EFI_APPLICATION 10u
#define OC_PE_SUBSYSTEM_EFI_BOOT_DRIVER 11u
#define OC_PE_SUBSYSTEM_EFI_RUNTIME_DRIVER 12u

typedef enum {
    OC_PE_OK = 0,
    OC_PE_TRUNCATED,
    OC_PE_BAD_DOS,
    OC_PE_BAD_SIGNATURE,
    OC_PE_BAD_MACHINE,
    OC_PE_BAD_OPTIONAL_HEADER,
    OC_PE_BAD_SUBSYSTEM,
    OC_PE_BAD_ALIGNMENT,
    OC_PE_BAD_SECTION,
    OC_PE_BAD_ENTRY_POINT,
    OC_PE_BAD_RELOCATION,
    OC_PE_LIMIT
} oc_pe_result;

typedef enum {
    OC_PE_ROLE_APPLICATION = 1,
    OC_PE_ROLE_DRIVER = 2
} oc_pe_role;

typedef struct {
    uint16_t machine;
    uint16_t subsystem;
    uint16_t section_count;
    uint32_t entry_rva;
    uint32_t image_size;
    uint32_t headers_size;
    uint32_t relocation_rva;
    uint32_t relocation_size;
} oc_pe_info;

oc_pe_result oc_pe_validate_x64_efi(const uint8_t *bytes, size_t size,
                                    size_t maximum_size, oc_pe_role role,
                                    oc_pe_info *info);
oc_pe_result oc_pe_validate_efi(const uint8_t *bytes, size_t size,
                                size_t maximum_size, oc_pe_role role,
                                uint16_t expected_machine, oc_pe_info *info);
oc_pe_result oc_pe_validate_native_efi(const uint8_t *bytes, size_t size,
                                       size_t maximum_size, oc_pe_role role,
                                       oc_pe_info *info);
const char *oc_pe_result_string(oc_pe_result result);

#endif
