# G0/G1 research and implementation plan

## Workstream A: reproducible UEFI execution

1. Build a freestanding PE32+ x64 EFI application without a host libc.
2. Boot it from the standard `EFI/BOOT/BOOTX64.EFI` removable-media path in
   QEMU/OVMF.
3. Capture a deterministic marker through QEMU debug I/O and fail the test on
   timeout, firmware load error, or missing marker.
4. Add GOP enumeration and framebuffer pattern tests, then screenshot/golden
   image comparison.
5. Add a writable FAT disk-image path when persistence tests begin. The initial
   directory-backed virtual FAT drive is test convenience, not release media.

## Workstream B: core launcher

Implement in independently testable increments: loaded-image device discovery,
filesystem access, bounded TOML parsing, path validation, game enumeration,
GOP presentation, input, and finally `LoadImage`/`StartImage` lifecycle tests.
Every parser and path-normalization routine should also have host-side unit and
fuzz tests. No game may be assumed trustworthy.

## Workstream C: GLES ABI and software feasibility

Freeze neither an ABI nor a GLES feature claim yet. First write ADRs covering
context creation/presentation, resolver versus function table, allocator and
ownership rules, and error/version negotiation. Audit a pinned Mesa revision at
source-symbol granularity for libc, TLS, thread, synchronization, compiler, and
platform dependencies. Compare that cost with a smaller software rasterizer
while keeping the proposed public ABI backend-neutral.

## Workstream D: Radeon research

Keep this source-audit-only until the core VM harness exists. Map Radeonsi,
AMDGPU winsys, libdrm, LLVM, and Linux kernel dependencies at pinned revisions.
The first hardware experiment is inventory-only EFI code. Any MMIO, DMA, reset,
power, firmware-loading, queue, or display-ownership experiment requires a
separate reviewed test plan and explicit device bounds.

## First decisions to record

- ADR 0001: build system and freestanding EFI ABI.
- ADR 0002: GLES binding/context/presentation ABI.
- ADR 0003: Mesa Softpipe viability.
- ADR 0004: Radeonsi DRM/winsys boundary.
- ADR 0005: GPU/GOP ownership.
- ADR 0006: shader compiler strategy.

The VM gate passes when a clean checkout builds and its EFI binary produces the
expected boot marker under OVMF. That is evidence only for the VM configuration,
not physical firmware compatibility.

