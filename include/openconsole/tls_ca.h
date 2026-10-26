#ifndef OPENCONSOLE_TLS_CA_H
#define OPENCONSOLE_TLS_CA_H

#include "openconsole/uefi.h"

#define OC_TLS_CA_MAX_DER_BYTES 8192u

EFI_STATUS oc_tls_ca_install(EFI_BOOT_SERVICES *, EFI_RUNTIME_SERVICES *,
                             const uint8_t *, UINTN);

#endif
