# ADR 0007: VM GPU strategy

- Status: Accepted for G0/G2 development
- Date: 2026-10-24

## Decision

Use three explicitly different VM tracks:

1. QEMU standard VGA or virtio-vga provides GOP for launcher and software GLES
   presentation.
2. QEMU `ati-vga,model=rv100` is attached as a second PCI device only for safe
   AMD vendor/device discovery and bounded PCI configuration/BAR tests.
3. `virtio-vga-gl`/virgl may later provide an accelerated VM-only GLES transport,
   but it is not an AMDGPU or Radeonsi hardware simulation.

Real Radeonsi/AMDGPU execution requires physical hardware or carefully reviewed
VFIO assignment. VFIO is not part of automated development: it changes device
ownership, depends on complete IOMMU groups and reset behavior, and could remove
a GPU from the host. No project command binds devices to `vfio-pci`.

## Why

QEMU 8.2.2 offers `ati-vga` models only for Rage 128 Pro (`1002:5046`) and RV100
(`1002:5159`). QEMU's own source describes this model as incomplete, 2D-only,
and without 3D. It cannot execute RDNA2 command streams, Navi 22 firmware, GPUVM,
or shader ISA.

Virtio-gpu's virgl backend can transport Gallium-style rendering commands to a
host renderer. This is useful for validating the OpenConsole ABI, context
lifecycle, and reference scenes with acceleration, while remaining a separate
backend from the eventual AMD implementation.

## Consequences

An OVMF test passing with `ati-vga` proves PCI discovery only. A virgl test proves
the virtio/virgl path only. Neither advances the G3 Navi 22 execution gate. Every
status report must preserve those distinctions.

