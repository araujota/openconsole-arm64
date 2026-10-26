#include "openconsole/uefi.h"
#include "openconsole/platform_debug.h"

#define EFI_GRAPHICS_OUTPUT_BLT_VIDEO_FILL 0u

static const EFI_GUID gop_guid =
    {0x9042a9deu, 0x23dcu, 0x4a38u, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

static void debug_putc(char value) {
    oc_platform_debug_putc(value);
}

static void debug_write(const char *text) {
    while (*text != '\0') debug_putc(*text++);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static const CHAR16 message[] =
        L"Input Probe child EFI is running. Press any key to return.\r\n";
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL blue = {112, 52, 20, 0};
    EFI_INPUT_KEY key;
    UINTN index = 0u;
    (void)image_handle;
    if (!EFI_ERROR(system_table->boot_services->locate_protocol(
            &gop_guid, 0, (void **)&gop))) {
        gop->blt(gop, &blue, EFI_GRAPHICS_OUTPUT_BLT_VIDEO_FILL,
                 0, 0, 0, 0, gop->mode->info->horizontal_resolution,
                 gop->mode->info->vertical_resolution, 0);
    }
    system_table->con_out->output_string(system_table->con_out, message);
    debug_write("OPENCONSOLE_CHILD_ABI_NEGOTIATED_UEFI_INPUT_1_0\n");
    debug_write("OPENCONSOLE_CHILD_READY\n");
    for (;;) {
        if (EFI_ERROR(system_table->boot_services->wait_for_event(
                1u, &system_table->con_in->wait_for_key, &index))) continue;
        if (!EFI_ERROR(system_table->con_in->read_key_stroke(system_table->con_in, &key))) break;
    }
    debug_write("OPENCONSOLE_CHILD_KEY_RECEIVED\n");
    return EFI_SUCCESS;
}
