# ADR 0005: GPU and GOP ownership

- Status: Research required
- Date: 2026-10-24

## Problem

Firmware may still own and use the GPU that backs GOP. GOP exposes display modes,
blits, and sometimes a linear framebuffer; it does not promise that the surface
is a GPU render target or that page flips, vsync, queue ownership, or destructive
reset are safe.

## Candidate presentation paths

1. Render on a non-display GPU and copy the finished frame through CPU-visible
   memory to GOP.
2. Render on the display GPU but copy/read back into a GOP-owned surface without
   changing display ownership.
3. Use a proven GPU copy into a compatible scanout allocation.
4. Take over native display management only as a separately gated later design.

## Acceptance evidence

Inventory handles, PCI devices, GOP modes, framebuffer format/range, and monitor
routing on each tested machine. Prove the selected path survives repeated game
transitions and restores display state. A path requiring unbounded DMA or reset
of the firmware display device is a NO-GO until ownership can be made explicit.

