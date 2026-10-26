#include "openconsole/tls_ca.h"

#define EFI_VARIABLE_NON_VOLATILE 1u
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 2u
#define EFI_VARIABLE_RUNTIME_ACCESS 4u
#define EFI_SIGNATURE_LIST_BYTES 28u
#define EFI_SIGNATURE_OWNER_BYTES 16u

static const EFI_GUID tls_ca_guid =
    {0xfd2340d0u,0x3dabu,0x4349u,{0xa6,0xc7,0x3b,0x4f,0x12,0xb4,0x8e,0xae}};
static const EFI_GUID cert_x509_guid =
    {0xa5c059a1u,0x94e4u,0x4aa7u,{0x87,0xb5,0xab,0x15,0x5c,0x2b,0xf0,0x72}};
static const EFI_GUID owner_guid =
    {0x9698dc92u,0x3a72u,0x49d9u,{0xa7,0x37,0x35,0x78,0xf1,0x5d,0x35,0x3d}};
static CHAR16 variable_name[] = {'T','l','s','C','a','C','e','r','t','i','f','i','c','a','t','e',0};

static void copy(void *destination, const void *source, UINTN size) {
    uint8_t *out = destination; const uint8_t *in = source;
    while (size-- != 0u) *out++ = *in++;
}
static void w32(uint8_t *out, uint32_t value) {
    unsigned i; for (i = 0u; i < 4u; ++i) out[i] = (uint8_t)(value >> (8u * i));
}

EFI_STATUS oc_tls_ca_install(EFI_BOOT_SERVICES *boot, EFI_RUNTIME_SERVICES *runtime,
                             const uint8_t *der, UINTN der_size) {
    uint8_t *database = 0;
    UINTN database_size;
    EFI_STATUS status;
    if (boot == 0 || runtime == 0 || runtime->set_variable == 0 || der == 0 ||
        der_size == 0u || der_size > OC_TLS_CA_MAX_DER_BYTES ||
        der_size > (UINTN)0xffffffffu - EFI_SIGNATURE_LIST_BYTES - EFI_SIGNATURE_OWNER_BYTES)
        return EFI_INVALID_PARAMETER;
    database_size = EFI_SIGNATURE_LIST_BYTES + EFI_SIGNATURE_OWNER_BYTES + der_size;
    status = boot->allocate_pool(EFI_LOADER_DATA, database_size, (void **)&database);
    if (EFI_ERROR(status)) return status;
    copy(database, &cert_x509_guid, sizeof(cert_x509_guid));
    w32(database + 16u, (uint32_t)database_size);
    w32(database + 20u, 0u);
    w32(database + 24u, (uint32_t)(EFI_SIGNATURE_OWNER_BYTES + der_size));
    copy(database + EFI_SIGNATURE_LIST_BYTES, &owner_guid, sizeof(owner_guid));
    copy(database + EFI_SIGNATURE_LIST_BYTES + EFI_SIGNATURE_OWNER_BYTES, der, der_size);
    status = runtime->set_variable(variable_name, &tls_ca_guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
        EFI_VARIABLE_RUNTIME_ACCESS, database_size, database);
    (void)boot->free_pool(database);
    return status;
}
