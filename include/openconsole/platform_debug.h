#ifndef OPENCONSOLE_PLATFORM_DEBUG_H
#define OPENCONSOLE_PLATFORM_DEBUG_H

#include <stdint.h>
#include "openconsole/target.h"

static inline void oc_platform_debug_putc(char value) {
#if OC_TARGET_ARCH_X64
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x402));
#else
    volatile uint32_t *const pl011_data = (volatile uint32_t *)(uintptr_t)0x09000000u;
    volatile uint32_t *const pl011_flags = (volatile uint32_t *)(uintptr_t)0x09000018u;
    while ((*pl011_flags & (1u << 5u)) != 0u) { }
    *pl011_data = (uint32_t)(uint8_t)value;
#endif
}

static inline void oc_platform_qemu_exit(uint32_t value) {
#if OC_TARGET_ARCH_X64
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"((uint16_t)0xf4));
#else
    (void)value;
    register uint64_t function_id __asm__("x0") = 0x84000008u;
    __asm__ volatile("hvc #0" : "+r"(function_id) : : "memory");
#endif
}

#endif
