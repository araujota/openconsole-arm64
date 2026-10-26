#include <stddef.h>
#include <stdint.h>
#include "openconsole/gles2_protocol.h"
#include "openconsole/net_protocol.h"
#include "openconsole/target.h"
#include "openconsole/uefi.h"

_Static_assert(sizeof(void *) == 8u, "OpenConsole targets require 64-bit pointers");
_Static_assert(sizeof(UINTN) == sizeof(void *), "UINTN must be native width");
_Static_assert(sizeof(EFI_STATUS) == sizeof(UINTN), "EFI status must be native width");
_Static_assert(sizeof(EFI_GUID) == 16u, "EFI GUID layout drift");
_Static_assert(sizeof(EFI_INPUT_KEY) == 4u, "EFI input key layout drift");
_Static_assert(sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) == 4u, "GOP pixel layout drift");
_Static_assert(sizeof(OC_GLES2_PROTOCOL *) == sizeof(void *), "GLES protocol pointer drift");
_Static_assert(sizeof(OC_NET_PROTOCOL *) == sizeof(void *), "network protocol pointer drift");
_Static_assert(OC_TARGET_PE_MACHINE == OC_PE_MACHINE_X64 ||
               OC_TARGET_PE_MACHINE == OC_PE_MACHINE_ARM64, "unknown PE target");

int target_abi_compile_test(void) { return OC_TARGET_ARCH_X64 ? 64 : 164; }
