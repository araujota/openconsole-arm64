# Installed games and child lifecycle

This document is normative for OpenConsole game format 2.0.

## Installed view

The user-visible selected version is a logical view:

```text
games/<id>/
├── game.toml
├── <entry and assets exactly as indexed by OCG2>
└── <selected package bytes>
```

This view is not a second mutable copy. Its physical files are the selected
`EFI/OpenConsole/State/Games/<id>/slot-{a,b}/package/` tree, alongside retained
`bundle.ocg`, `descriptor.ocgi`, and redundant `select-{a,b}.ocgs` records.
`<id>` exactly equals the manifest ID. Discovery ignores a game without a valid
selected record, current publisher authorization, and complete slot.

## Locating the package root without an SDK runtime

A child receives its own `EFI_HANDLE`. It locates
`EFI_LOADED_IMAGE_PROTOCOL`, obtains `DeviceHandle` and `FilePath`, resolves
`EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` on that same device, opens the volume, and
removes the final entry-path components from its loaded device path. The result
is its selected package root. It MUST NOT search disks by ordinal or embed
`games/<id>`.

The repository provides complete C source plus Zig/Rust declarations for this
sequence. They are examples, not a required binary runtime or proprietary SDK.

## Before launch

The parent:

1. recovers transactional state and selects the highest valid generation;
2. evaluates required/optional capability versions;
3. reopens the manifest and entry from its own loaded-image volume;
4. verifies the signed inventory, exact bytes, PE role, and bundle ID;
5. persists a launch-attempt record and configures a supported firmware
   watchdog;
6. begins parent-owned graphics/network/audio resource generations;
7. calls `LoadImage` from the verified buffer and then `StartImage`.

No discovery-time cache authorizes launch.

## While running

Children run before `ExitBootServices` and cooperatively use UEFI events,
timers, keyboard, Simple Pointer, and versioned OpenConsole protocols. Calls are
single-threaded unless a protocol explicitly says otherwise. Network and audio
need periodic pump calls. A game owns its own ordinary allocations but provider
objects remain provider-owned.

Writable game data is not standardized in 2.0; games MUST treat their package
as read-only. Firmware variables, arbitrary volume writes, driver installation,
runtime-service hooks, reset requests, direct device programming, DMA, and
parent-path access are outside the game contract and are blocking analyzer
findings unless a future reviewed capability explicitly grants them.

## Return

Before returning from `efi_main`, a child MUST stop/release operations, destroy
voices and GLES contexts, close files/events it created, and restore any
standard state it directly changed. It returns an EFI status; it MUST NOT call
`ExitBootServices`.

The parent cancels and destroys any remaining generation-owned resources,
disables its watchdog, records attempt completion, resets pointer state,
restores its GOP mode and dashboard, frees `ExitData`, unloads the child, and
rescans authenticated game selections. Error return is reported but follows the
same cleanup path.

## Hang and recovery boundary

`StartImage` is synchronous and EFI provides no reliable process isolation.
The parent may request a firmware watchdog reset, but cannot safely preempt
hostile or malfunctioning code. If the next boot sees an incomplete attempt, it
quarantines that exact bundle ID and offers safe mode, details, deletion, or a
manual retry. Safe mode loads only the signed baseline driver set.

OpenConsole games are trusted native firmware applications. Bundle signatures
identify publishers; static analysis detects broad indicators; neither is a
sandbox, proof of correctness, nor protection against all malicious behavior.
