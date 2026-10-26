#include "openconsole/uefi.h"
#include "openconsole/platform_debug.h"

static void debug_putc(char value) {
#if OC_TARGET_ARCH_X64
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)value), "Nd"((uint16_t)0x402));
#else
    (void)value;
#endif
}

static void debug_write(const char *text) {
    while (*text != 0) debug_putc(*text++);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    (void)image_handle;
    (void)system_table;
    debug_write("OPENCONSOLE_BOOTNEXT_NORMAL_REACHED\n");
    oc_platform_qemu_exit(0x11);
    return EFI_SUCCESS;
}
