# ADR 0006: shader compiler strategy

- Status: Research required
- Date: 2026-10-24

## Options

- Runtime Mesa/LLVM compilation in UEFI.
- Mesa frontend with a smaller target compiler where technically valid.
- Offline compilation plus a constrained, versioned shader package.
- A deliberately narrow software-backend compiler/interpreter for the supported
  GLES subset.

## Evaluation criteria

Measure image size, initialization and compile latency, allocator/libc/TLS/thread
dependencies, CPU feature assumptions, deterministic output, cache format and
validation, license obligations, and whether independently packaged games remain
portable. Offline shaders may reduce runtime complexity but cannot be called
general GLES2 unless source-level shader semantics and target compatibility are
preserved.

## Dependency

Accept only after ADR 0003 and ADR 0004 produce pinned source maps. Software and
hardware backends may choose different internal compilers, but must expose the
same negotiated public behavior from ADR 0002.

