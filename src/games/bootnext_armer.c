#include "openconsole/uefi.h"
#include "openconsole/platform_debug.h"

typedef void(EFIAPI *EFI_RESET_SYSTEM_FN)(uint32_t, EFI_STATUS, UINTN, void *);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static CHAR16 boot_next[] = {'B','o','o','t','N','e','x','t',0};
    static EFI_GUID global = {0x8be4df61u, 0x93cau, 0x11d2u,
                                    {0xaau,0x0du,0x00u,0xe0u,0x98u,0x03u,0x2bu,0x8cu}};
    uint16_t entry = 6u;
    EFI_STATUS status;
    EFI_RESET_SYSTEM_FN reset;
    (void)image_handle;
    status = system_table->runtime_services->set_variable(
        boot_next, &global, 0x00000007u, sizeof(entry), &entry);
    if (EFI_ERROR(status)) return status;
    oc_platform_debug_putc('B');
    oc_platform_debug_putc('N');
    oc_platform_debug_putc('\n');
    reset = (EFI_RESET_SYSTEM_FN)system_table->runtime_services->reset_system;
    reset(0u, EFI_SUCCESS, 0u, 0);
    return EFI_SUCCESS;
}
