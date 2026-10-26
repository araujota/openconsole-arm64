#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openconsole/tls_ca.h"

static uint8_t captured[256];
static UINTN captured_size;
static EFI_STATUS EFIAPI allocate(uint32_t type, UINTN size, void **out) {
    (void)type; *out = malloc((size_t)size); return *out ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS EFIAPI release(void *memory) { free(memory); return EFI_SUCCESS; }
static EFI_STATUS EFIAPI set_variable(CHAR16 *name, const EFI_GUID *guid,
                                      uint32_t attributes, UINTN size, void *data) {
    static const CHAR16 expected[] = {'T','l','s','C','a','C','e','r','t','i','f','i','c','a','t','e',0};
    unsigned i = 0; (void)guid;
    while (expected[i] || name[i]) { assert(expected[i] == name[i]); ++i; }
    assert(attributes == 7u && size <= sizeof(captured));
    memcpy(captured, data, (size_t)size); captured_size = size;
    return EFI_SUCCESS;
}
static uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int main(void) {
    EFI_BOOT_SERVICES boot; EFI_RUNTIME_SERVICES runtime;
    static const uint8_t der[] = {0x30,0x03,0x02,0x01,0x01};
    memset(&boot, 0, sizeof(boot)); memset(&runtime, 0, sizeof(runtime));
    boot.allocate_pool = allocate; boot.free_pool = release; runtime.set_variable = set_variable;
    assert(oc_tls_ca_install(&boot, &runtime, der, sizeof(der)) == EFI_SUCCESS);
    assert(captured_size == 28u + 16u + sizeof(der));
    assert(r32(captured + 16) == captured_size && r32(captured + 20) == 0u);
    assert(r32(captured + 24) == 16u + sizeof(der));
    assert(memcmp(captured + 44, der, sizeof(der)) == 0);
    assert(oc_tls_ca_install(&boot, &runtime, der, 0) == EFI_INVALID_PARAMETER);
    puts("PASS: bounded EFI_SIGNATURE_LIST TLS CA variable construction");
    return 0;
}
