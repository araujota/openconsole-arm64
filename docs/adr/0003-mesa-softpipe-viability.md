# ADR 0003: Mesa Softpipe viability

- Status: Research required
- Date: 2026-10-24

## Question

Can a pinned Mesa Softpipe/Gallium configuration provide the declared GLES2
subset in UEFI without turning the launcher into an operating-system emulation
layer?

## Audit method

Pin a Mesa commit and exact Meson options. Starting from the EGL/GLES-facing
entry points and Softpipe screen/context creation, produce a symbol-level graph
of runtime dependencies on allocation, files, environment, dynamic loading,
TLS, threads, atomics, clocks, synchronization, shader compilation, and compiler
runtime helpers. Separate build-host tools from code required in the EFI image.

## Decision rule

Choose Mesa only if the required compatibility surface is bounded, testable, and
smaller-risk than implementing the supported GLES subset elsewhere. A successful
host build is not a pass. The proof is reference scenes running in OVMF with GOP
presentation and clean context reinitialization. Otherwise pivot to a smaller
free-software rasterizer while preserving ADR 0002's public ABI.

