# Initial pinned-source audit

Date: 2026-10-24. Snapshot revisions are recorded in
[`sources.lock.toml`](sources.lock.toml). This is the first dependency slice,
not the completed G0 symbol graph.

## Softpipe creation path

At Mesa `c515ec0`,
[`softpipe_create_screen`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/c515ec0fe77da9f42f390e442379f2d0629d908c/src/gallium/drivers/softpipe/sp_screen.c#L441)
allocates a `softpipe_screen`, stores a supplied `sw_winsys`, installs Gallium
callbacks, and selects `softpipe_create_context`. The context constructor in
[`sp_context.c`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/c515ec0fe77da9f42f390e442379f2d0629d908c/src/gallium/drivers/softpipe/sp_context.c#L184)
initializes math support, shader sampler/image/buffer machinery, tile and texture
caches, the TGSI fragment execution machine, draw stages, and uploaders.

Immediate UEFI replacement/audit surfaces visible in this slice:

| Source/symbol | Dependency class | UEFI question |
| --- | --- | --- |
| `softpipe_create_screen` → `CALLOC_STRUCT` | runtime allocation | Route through bounded EFI pool allocation or an owned arena; establish overflow and lifetime rules. |
| `debug_get_option_sp_debug` | runtime environment/config | Compile out environment access or supply immutable configuration. |
| supplied `struct sw_winsys` | runtime platform interface | Implement memory display targets and a GOP presentation adapter. |
| `u_default_get_timestamp` | runtime clock | Map to an available monotonic UEFI timer and document precision/wrap behavior. |
| `softpipe_create_context` → `util_init_math` | runtime compiler/libm/CPU | Audit floating-point environment and compiler-runtime symbols. |
| tile/texture caches and uploaders | runtime allocation | Measure fixed and scene-dependent memory; test complete destruction/recreation. |
| TGSI execution machine | runtime shader execution | Trace NIR/TGSI lowering and GLSL frontend before claiming GLES2 shader support. |

This is enough to reject the notion that Softpipe is only a raster loop, but not
enough to reject Mesa. The next slice must start from the GLES API/state tracker
and trace through shader translation to these Gallium entry points.

## Radeonsi to DRM boundary

At the same Mesa revision,
[`si_pipe.c`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/c515ec0fe77da9f42f390e442379f2d0629d908c/src/gallium/drivers/radeonsi/si_pipe.c#L310)
constructs AMDGPU winsys options and calls `amdgpu_winsys_create` for DRM major
version 3. That constructor in
[`amdgpu_winsys.c`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/c515ec0fe77da9f42f390e442379f2d0629d908c/src/gallium/winsys/amdgpu/drm/amdgpu_winsys.c#L389)
is explicitly file-descriptor and DRM-device based. In its first initialization
path it:

- calls `ac_drm_device_initialize` and may open a render node;
- identifies/deduplicates DRM devices and file descriptions;
- creates a VM timeline sync object;
- initializes BO caches and slab allocators;
- creates numerous mutexes and a command-submission worker queue;
- optionally reserves a VMID.

At libdrm `b97cbde1`, buffer creation/VA operations and submission are ioctl
wrappers: `amdgpu_bo_alloc` reaches `DRM_AMDGPU_GEM_CREATE`, GPU VA mapping
reaches `DRM_AMDGPU_GEM_VA`, and submission reaches `DRM_AMDGPU_CS` in
[`amdgpu_bo.c`](https://gitlab.freedesktop.org/mesa/drm/-/blob/b97cbde15c5c3abfe44d78e8f57139e50f612fec/amdgpu/amdgpu_bo.c)
and
[`amdgpu_cs.c`](https://gitlab.freedesktop.org/mesa/drm/-/blob/b97cbde15c5c3abfe44d78e8f57139e50f612fec/amdgpu/amdgpu_cs.c).
Those ioctls are services implemented by the Linux AMDGPU kernel driver, not a
transport that UEFI PCI I/O can forward unchanged.

### Preliminary decision

**PIVOT the framing, not the goal:** retain Radeonsi as a potential command and
shader-generation source, but treat the entire kernel DRM contract—device
initialization, firmware/IP discovery, BO allocation, GPUVM, queues/submission,
fences/sync objects, reset and fault recovery—as missing platform functionality.
No “small winsys shim” estimate is credible from this evidence. Do not begin a
complete driver; continue decomposing the contract and build inventory-only EFI
tools first.

## License inventory status

This remains preliminary and must become a file-level bill of materials for any
code actually reused:

| Project | Top-level indication | Status |
| --- | --- | --- |
| Mesa | MIT-style project licensing with per-file notices | File-level audit pending |
| libdrm | MIT-style licenses with per-file notices | File-level audit pending |
| Linux | GPL-2.0-only kernel; UAPI carries Linux syscall note | Reference-only unless architecture/legal review changes |
| LLVM | Apache-2.0 with LLVM exceptions | Component/file audit pending |
| EDK II | BSD-2-Clause-Patent project policy, packages may vary | File-level audit pending |
| tinygrad | MIT project license | Reference-only; exact device-path audit pending |

No redistribution conclusion follows from this table.

