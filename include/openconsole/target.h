#ifndef OPENCONSOLE_TARGET_H
#define OPENCONSOLE_TARGET_H

#define OC_PE_MACHINE_X64 0x8664u
#define OC_PE_MACHINE_ARM64 0xaa64u

#if defined(__x86_64__) || defined(_M_X64)
#define OC_TARGET_ARCH_X64 1
#define OC_TARGET_ARCH_ARM64 0
#define OC_TARGET_ARCH_NAME "x64"
#define OC_TARGET_BOOT_FILENAME "BOOTX64.EFI"
#define OC_TARGET_OCG2_ENTRY_ROLE "entry-x64"
#define OC_TARGET_OCG2_ENTRY_ROLE_ID 2u
#define OC_TARGET_PE_MACHINE OC_PE_MACHINE_X64
#elif defined(__aarch64__) || defined(_M_ARM64)
#define OC_TARGET_ARCH_X64 0
#define OC_TARGET_ARCH_ARM64 1
#define OC_TARGET_ARCH_NAME "arm64"
#define OC_TARGET_BOOT_FILENAME "BOOTAA64.EFI"
#define OC_TARGET_OCG2_ENTRY_ROLE "entry-arm64"
#define OC_TARGET_OCG2_ENTRY_ROLE_ID 6u
#define OC_TARGET_PE_MACHINE OC_PE_MACHINE_ARM64
#else
#error "OpenConsole requires an x64 or ARM64 UEFI target"
#endif

#endif
