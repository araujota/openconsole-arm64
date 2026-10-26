# Virgl guest / virtio-gpu UEFI dependency map

Pinned inputs are Mesa `c515ec0` and Linux `5b0764a5`; the running VM evidence
uses QEMU 8.2.2 `11aa0b1f`. The host's virglrenderer is outside the EFI image.

| Guest layer | Conventional dependency | Required UEFI replacement |
| --- | --- | --- |
| Gallium virgl driver | Mesa `pipe_screen`/`pipe_context`, resource and command encoders | Either audited Mesa subset or a protocol-specific encoder behind `OC_GLES_BACKEND` |
| Virgl winsys | DRM render-node fd, ioctls, mmap, sync handles | Cannot be reused; implement firmware-native virtio-gpu transport |
| Capability discovery | `VIRTIO_GPU_CMD_GET_CAPSET_INFO/GET_CAPSET` | PCI capability traversal, feature negotiation, control queue commands |
| Resource storage | GEM/resource handles and mappings | EFI-owned pages, resource-create/attach-backing commands, bounded scatter lists |
| Command submission | DRM virtgpu execbuffer | Virgl command buffer plus `VIRTIO_GPU_CMD_SUBMIT_3D` on control virtqueue |
| Presentation | resource flush / transfer / scanout | Transfer/flush as required, then GOP remains OpenConsole's portable sink until explicit scanout ownership exists |
| Completion | poll/fence/syncobj | Cooperative virtqueue completion with bounded timeout and device-status checks |
| Interrupt/threading | poll, pthreads, worker queues | Initial polling transport only; no scheduler or hidden thread emulation |
| Reset/recovery | kernel virtio reset and cleanup | Explicit device-state recovery and launcher software fallback |

The necessary wire definitions are in the virtio-gpu specification/UAPI, but a
safe implementation also needs generic virtio PCI discovery, feature/status
negotiation, queue allocation, descriptor management, DMA address handling,
memory ordering, notifications, timeouts, and teardown. QEMU
`virtio-vga-gl` booting GOP does not provide any of those guest 3D services.

## Decision

**GO for a separate VM-only transport prototype; PIVOT away from Mesa's DRM
winsys.** A future change may implement the narrow virtio PCI/control-queue
services and evaluate reusing the virgl encoder. It must remain labeled VM-only
and must fall back to this change's software backend. No virgl or virtio command
transport is linked by the present change.
