#include "package_root.h"
#include "openconsole/gles2_protocol.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table) {
    EFI_FILE_PROTOCOL *package_root = 0;
    OC_GLES2_PROTOCOL *gles = 0;
    EFI_STATUS status = oc_example_open_package_root(image, system_table, &package_root);
    if (EFI_ERROR(status)) return status;
    /* Protocols are optional unless the manifest marks them required. A real
     * game creates/destroys its context and pumps other services before return. */
    status = system_table->boot_services->locate_protocol(
        &OC_GLES2_PROTOCOL_GUID, 0, (void **)&gles);
    if (!EFI_ERROR(status) && gles != 0 && system_table->con_out != 0)
        (void)system_table->con_out->output_string(
            system_table->con_out, L"OpenConsole portable C example\r\n");
    package_root->close(package_root);
    return EFI_SUCCESS;
}
