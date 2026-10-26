# ADR 0001: freestanding x64 EFI build and OVMF harness

- Status: Accepted for scaffold
- Date: 2026-10-24

## Context

The project needs a fast VM loop before choosing a larger firmware framework or
graphics stack. The host already supplies Clang, GNU binutils, QEMU, and OVMF. Pulling a
large dependency into the first artifact would obscure whether the PE/COFF and
firmware execution path itself works.

## Decision

Build the initial application as freestanding C, use the Microsoft x64 calling
convention required by x64 UEFI, and link directly to a PE32+ EFI application
with GNU ld's `i386pep` emulation. Keep the local UEFI declarations minimal and add compile-time layout
checks as protocol coverage grows. Use Q35 plus split OVMF CODE/VARS pflash.
Each run gets a fresh copy of the variable-store template.

The automated image is separate from the interactive image. It writes a stable
marker to QEMU debug I/O port `0x402`, then terminates QEMU through the test-only
`isa-debug-exit` device. Production code must not depend on either mechanism.

## Consequences

The smoke-test has very few dependencies and boots quickly. It is not an EDK II
application and cannot yet reuse EDK II libraries or protocol declarations.
Before substantial UEFI surface area is implemented, compare continuing this
small compatibility layer with adopting EDK II or another maintained header and
library set. Protocol structure layouts must be checked against the pinned UEFI
specification.
