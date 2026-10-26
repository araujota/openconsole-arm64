# OpenConsole boot-volume format 1.0

The volume is FAT-compatible and is the device from which the running parent
was loaded. Paths are case-insensitive for collision and authorization checks,
even when a firmware preserves case.

```text
EFI/
├── BOOT/
│   └── BOOTX64.EFI
└── OpenConsole/
    ├── platform.ocp
    ├── Drivers/
    │   ├── Input/
    │   ├── Network/
    │   ├── Audio/
    │   ├── Crypto/
    │   └── Graphics/
    ├── Trust/
    │   ├── PlatformInventory.ed448.pub
    │   ├── PublisherKeys.ocpk
    │   └── ca.der                 # optional profile-specific TLS root
    ├── State/
    │   ├── Games/<id>/{slot-a,slot-b,select-a.ocgs,select-b.ocgs}/
    │   ├── Launch/{attempt-a.oclr,attempt-b.oclr}
    │   └── Recovery/
    ├── Updates/
    │   ├── slot-a.efi
    │   ├── slot-b.efi
    │   ├── select-a.ocu
    │   └── select-b.ocu
    ├── Logs/
    └── Notices/
games/<id>/               conceptual selected view; no duplicate mutable tree
```

## Ownership

`EFI`, `EFI/OpenConsole`, `.openconsole*`, `OPENCONSOLE-*`, and every
case-fold equivalent are parent-reserved. Bundles cannot name them. Install and
removal code receives an already confined game-state root and never constructs
a path from an unconstrained user string. Games treat all package paths as
read-only.

Logs are bounded, rotate by fixed slot, and contain no private keys or raw
uploaded content. State and logs are optional on read-only media; inability to
persist a required transaction or launch attempt blocks that operation rather
than degrading silently.

## `platform.ocp`

The signed platform inventory binds:

- layout major/minor and parent version;
- image profile: `development`, `test`, or future `release`;
- normalized path, role, size, SHA-256, version, and key scope for each
  parent-loaded image and trust file;
- the baseline safe-mode driver set;
- source/SBOM inventory digests;
- whether unsigned local games are explicitly permitted.

The canonical representation and signature rules mirror OCG2 but use domain
`OPENCONSOLE-PLATFORM-SIGNED\0` and platform-scoped keys. The parent verifies a
driver immediately before `LoadImage`. An adjacent checksum text file is build
evidence only.

## Profiles

The canonical `development` image contains the parent, integrity provider,
baseline USB pointer support, the packaged network stack necessary for catalog
access over firmware SNP/UNDI, documented trust placeholders, notices, and
signed example games. Audio and hardware-specific graphics drivers may be
present when inventoried but absence is nonfatal.

`test` images may contain fixture CAs, private test publisher keys, probes,
debug-exit devices, and deterministic endpoints. Their platform manifest and UI
MUST visibly say `TEST`; those assets MUST NOT appear in development or release
profiles.

No profile currently claims Secure Boot enrollment. Development images are
unsigned unless release evidence explicitly says otherwise.

## Image reproducibility

The image builder sorts normalized paths, fixes timestamps and volume metadata,
uses pinned inputs, and emits:

- the FAT image;
- `platform.ocp` and its detached public verification inputs;
- `SHA256SUMS` for distribution transport;
- a machine-readable source/dependency/patch inventory;
- SPDX or CycloneDX SBOM;
- a profile/evidence statement.

Two clean builds with identical declared inputs MUST have identical file
inventories and image bytes. A comparison tool reports the first metadata or
content divergence rather than only a final hash mismatch.
