/* SPDX-License-Identifier: Apache-2.0 */
#include "openconsole/uefi.h"

static void debug_putc(char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x402));
}

static void debug_write(const char *text) {
    while (*text != '\0') debug_putc(*text++);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    (void)image_handle;
    (void)system_table;
    debug_write("OPENCONSOLE_HANG_CHILD_READY\n");
    for (;;) __asm__ volatile("pause");
}
