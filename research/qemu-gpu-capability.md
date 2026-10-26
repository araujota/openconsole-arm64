# QEMU GPU capability finding

## Tested host

- QEMU package: 8.2.2 (`1:8.2.2+ds-0ubuntu1.18`)
- Upstream QEMU 8.2.2 commit: `11aa0b1ff115b86160c4d37e7c37e6a6b13b77ea`
- OVMF package: 2024.02

## Matrix

| VM device | OVMF/GOP result | 3D meaning | AMD fidelity |
| --- | --- | --- | --- |
| `VGA` | Works; current menu screenshot path | GOP/2D only | None |
| `ati-vga,model=rv100` alone | Parent starts, but no usable GOP was located | QEMU model has no 3D | RV100 PCI/limited 2D only |
| `VGA` + secondary `ati-vga,model=rv100` | Works; EFI probe reads `1002:5159` | PCI discovery tests only | Not R600/GCN/RDNA |
| `virtio-vga-gl` + `egl-headless` | Works with GOP on this host | Virgl host-rendered acceleration is possible after a guest driver exists | None; protocol GPU |
| VFIO physical AMD GPU | Not attempted | Real device | Exact assigned hardware only |

The upstream
[`ati.c`](https://gitlab.com/qemu-project/qemu/-/blob/11aa0b1ff115b86160c4d37e7c37e6a6b13b77ea/hw/display/ati.c)
header says the model targets Rage 128 Pro and some RV100, is currently little
more than a framebuffer, starts with 2D, and has no 3D. The implementation accepts
only device IDs `0x5046` and `0x5159`. Therefore it cannot validate Radeonsi or
the Navi 22 driver plan.

[QEMU's virtio-gpu documentation](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html)
distinguishes the 2D backend from virglrenderer and rutabaga accelerated
backends. Virgl translates a guest intermediate representation to host OpenGL;
it does not emulate an AMD register-level GPU.

## Implemented probe

`src/gpu/pci_probe.c` uses `EFI_PCI_IO_PROTOCOL` read operations only. It locates
PCI handles, reads the first 16 configuration bytes, filters vendor `0x1002`, and
records device/class identity. The OVMF interaction test requires
`OPENCONSOLE_AMD_PCI_1002_5159` before passing. It performs no BAR mapping, MMIO,
bus mastering, DMA, reset, firmware load, or power operation.

## Next technical slice

1. Implement software GLES context/presentation behind the versioned bootstrap
   ABI and pass clear/triangle/depth/blend reference scenes through GOP.
2. Audit whether Mesa's virgl guest code can be isolated for UEFI or whether a
   smaller VM-only command encoder is more realistic.
3. Expand the AMD EFI probe to read-only BAR descriptors and PCI location, with
   strict bounds and no enable/write operations.
4. Defer all real AMD queue work until an explicit physical-hardware plan exists.

