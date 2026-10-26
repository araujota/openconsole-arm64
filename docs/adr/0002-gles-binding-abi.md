# ADR 0002: GLES binding, context, and presentation ABI

- Status: Proposed; decision deferred pending prototypes
- Date: 2026-10-24

## Decision drivers

Games need ordinary GLES2 C signatures, but GLES does not create contexts or
define presentation. The ABI must work with both software and hardware backends,
remain versionable, and make allocator and object ownership unambiguous across
independently built EFI images.

## Candidate direction

Expose one versioned protocol discovered by GUID. A small bootstrap function
returns a size-tagged function table containing context lifecycle, symbol
resolution/capability queries, presentation, and the GLES2 entry points. Use
fixed-width fields at the boundary and the platform `EFIAPI` calling convention.
Require callers to negotiate major/minor ABI versions and ignore unknown tail
fields. Backend objects remain owned by the provider; cross-image allocation and
freeing are forbidden unless an allocator pair is explicitly supplied.

## Proofs required before acceptance

- C and independently generated Rust/Zig ABI layout tests.
- Context create/destroy/recreate and provider unload rules.
- Defined behavior for unsupported entry points, lost contexts, and game exit.
- Presentation semantics for GOP copy, pitch, pixel format, and synchronization.
- A minimal textured-triangle client running unchanged on two mock backends.

