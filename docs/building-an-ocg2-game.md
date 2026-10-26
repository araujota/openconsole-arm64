<!-- SPDX-License-Identifier: Apache-2.0 -->
# Build and sign an OCG2 game

This is the shortest complete developer path from an ordinary x64 EFI
application to an installable OpenConsole bundle. OpenConsole does not require
a compiler wrapper, runtime, package manager, or generated client library.
Games use standard UEFI definitions plus any versioned protocol headers they
choose from `include/openconsole/`.

## 1. Build the EFI application

Build a PE32+ x64 EFI **application**, not a driver. The entry point uses the
ordinary UEFI signature:

```c
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table);
```

Use the Loaded Image device path to open files relative to the installed
package. Do not compile a game ID or `EFI/OpenConsole/State/Games/...` path into
the child. `examples/sdk/c/package_root.c` is a portable example; the behavior
can be implemented directly in C, Zig, Rust, or another freestanding language.

Children execute as trusted native firmware code. They must destroy their GLES
contexts, cancel network work, stop audio voices, close files/events, and return
from `StartImage`. There is no process sandbox.

## 2. Write `game.toml`

```toml
format_version = "2.0"
package_format = "ocg2"
id = "orbit"
title = "Orbit"
version = "1.2.3"
entry = "bin/orbit.efi"
architecture = "x64"
required_capabilities = ["uefi.input.keyboard@1.0"]
optional_capabilities = ["openconsole.net@0.1", "openconsole.audio@0.2"]
graphics_api = "gop"
accel = "forbidden"
min_graphics_abi = 0
developer = "Example Studio"
description = "A small orbital puzzle game."
jacket = "assets/jacket.ocj"
controls = "assets/controls.cfg"
```

Required capabilities block launch when absent. Optional capabilities must be
tolerated when unavailable. Use `graphics_api = "gles2"` and
`openconsole.gles2@0.1` only when using the experimental GLES-facing protocol.
Acceleration policy is `forbidden`, `optional`, or `required`; it is not a GLES
conformance or physical-GPU guarantee.

The manifest is a strict, bounded TOML-compatible profile. Tables, dotted keys,
floats, dates, multiline strings, duplicate keys, and unknown required syntax
are intentionally unsupported in firmware.

## 3. Prepare optional assets

- Jackets use bounded `OCJ1`: an eight-byte header followed by row-major
  BGRA8888 pixels, at most 512×512 and 1 MiB. Convert PNG/JPEG on the host.
- Controls use the `controls-v1` text grammar documented in
  `game-manifest-v2.md`.
- Levels, shaders, PCM, configuration, and other ordinary content use the
  repeatable `data` role.

All package paths are relative ASCII `/` paths. Absolute paths, traversal,
backslashes, case-insensitive collisions, and reserved parent namespaces are
rejected before installation.

## 4. Create and protect the publisher key

```sh
umask 077
openssl genpkey -algorithm ED448 -out publisher-private.pem
openssl pkey -in publisher-private.pem -pubout -out publisher-public.pem
```

Keep the private key outside the source tree, build output, CI artifacts, and
the OpenConsole USB. Back it up according to the publisher's release policy.
The public key is not secret.

The platform owner or catalog reviewer must separately enroll that public key
as an active `game-package` publisher in the platform-authorized
`PublisherKeys.ocpk`. A bundle, download response, or TLS certificate cannot
add its own trust root.

## 5. Pack deterministically

```sh
python3 tools/ocg2.py pack \
  game.toml \
  dist/orbit-1.2.3.ocg \
  --private-key publisher-private.pem \
  --file entry-x64=build/orbit.efi \
  --file jacket-ocj1=assets/orbit.ocj \
  --file controls-v1=assets/controls.cfg \
  --file data:assets/levels.dat=assets/levels.dat \
  --file data:shaders/world.vert=shaders/world.vert
```

The manifest supplies destination paths for `entry-x64`, `jacket-ocj1`, and
`controls-v1`. A `data:PACKAGE/PATH=HOST_FILE` mapping supplies its destination
explicitly. Host filenames and directory placement are otherwise irrelevant.

Identical manifest bytes, file bytes, package paths, roles, and publisher key
produce identical bundle bytes. OCG2 2.0 is uncompressed and supports one x64
EFI entry, one optional jacket, one optional controls file, and arbitrary
bounded data files.

## 6. Inspect and verify independently

```sh
python3 tools/ocg2.py inspect dist/orbit-1.2.3.ocg
python3 tools/ocg2.py verify dist/orbit-1.2.3.ocg \
  --public-key publisher-public.pem
```

`inspect` reports the envelope, bundle identity, publisher key ID, and indexed
files without making a trust claim. `verify` additionally checks the Ed448
signature and all indexed payload digests. Record the resulting bundle ID and
whole-file SHA-256 in release notes.

For reproducibility, pack twice in clean output directories and compare the
`.ocg` files byte-for-byte. Run the repository analyzer before upload; catalog
acceptance means only “accepted by this analyzer/rule version,” never “safe.”

## 7. What the parent does with it

The parent validates the complete bundle and enrolled publisher, writes the
original bundle, signed descriptor, and extracted package into an inactive
slot, flushes and reopens every file, and commits through a redundant
checksummed selector. It preserves the last-known-good slot until commit.

Immediately before `LoadImage`, the parent reopens and authenticates the
selected bundle again, validates the executable as an x64 EFI application, and
uses the real selected-slot device path. Cached dashboard metadata never grants
execution authority.

Normative details are in `game-bundle-v2.md`, `game-manifest-v2.md`, and
`game-package-lifecycle.md`. Those documents control if this tutorial and the
byte-level contract ever disagree.
