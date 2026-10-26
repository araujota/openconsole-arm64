# ADR 0004: Radeonsi DRM/winsys boundary

- Status: Research required; no implementation authorized by this ADR
- Date: 2026-10-24

## Question

Which exact services do a pinned Radeonsi/Mesa revision and its AMDGPU winsys
expect from libdrm and the Linux AMDGPU kernel driver, and which could safely be
provided in x64 UEFI for Navi 22?

## Required artifact

Build a symbol-level, source-file-cited dependency map spanning Radeonsi command
generation, winsys creation, BO/VA management, firmware, queues, submission,
fences, resets, synchronization, and shader compilation. Label each edge as
build-time or runtime and user-space or kernel-space. Record exact Mesa, libdrm,
Linux, LLVM, and firmware revisions and licenses.

## Guardrail

No conclusion may describe the missing kernel/winsys functionality as a “small
shim” without the completed map. No MMIO, DMA, firmware load, reset, queue, or
power experiment follows merely from accepting this research plan.

