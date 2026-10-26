# ARM64 development-preview readiness

Release: `v0.1.0-aavmf-preview.1`

The ARM64 suitcase is ready for an explicitly labeled AAVMF development
preview. It is not a physical-hardware release. The release artifact is built
from the public `araujota/openconsole-arm64` repository and uses the removable
media boot path `EFI/BOOT/BOOTAA64.EFI`.

## Verified gates

- Clean-clone AArch64 cross-build and host contract tests pass.
- The deterministic 64 MiB FAT image reproduces SHA-256
  `b4babc74487dd62e6045b512f8c393ace7b38f1bff85101dab9f47d51fbfb7de`.
- Recursive image inspection finds ARM64 PE machine `0xAA64` and EFI
  application subsystem 10 for every executable payload, with no x64 EFI.
- The ARM64 image now carries native, inventory-signed Input, Network, Audio,
  and Crypto driver roles matching the normalized x64 baseline. Host tests
  reject missing roles, x64 substitution, and post-signing driver mutation.
- ARM64 OCG2 bundles require `architecture = "arm64"`, role `entry-arm64`,
  and an ARM64 PE entry; mismatch and dual-entry vectors are rejected.
- QEMU `virt` with AAVMF boots the parent, discovers the fixture game, renders
  the dashboard, launches the child, negotiates the service ABI, receives
  injected keyboard input, returns to the parent, and shuts down cleanly.
- AAVMF also selects a unique marked external game-data volume while keeping
  integrity, input, networking, audio, trust, and platform assets on the boot
  volume. x64 OVMF additionally covers fallback, duplicate-marker ambiguity,
  launch/return, confined deletion, and authoritative rescan.
- Dashboard and child framebuffer captures, serial checkpoints, firmware hash,
  QEMU version, elapsed time, and capability claims are retained as evidence.
- Existing x64 host, package, analyzer, and image-contract tests remain green;
  the established x64 release metadata and OCG2 role identifiers are unchanged.

## Capability status

| Capability | ARM64 status | Basis |
| --- | --- | --- |
| Host build and packaging | Verified | Clean public clone |
| Parent boot and dashboard | AAVMF validated | QEMU `virt` + AAVMF |
| Child launch, ABI, keyboard input, return | AAVMF validated | Automated serial and framebuffer harness |
| Physical ARM64 hardware | Unverified | No device qualification run |
| Packaged networking drivers | AAVMF SNP validated | Native ARM64 PE and signed role coverage; AAVMF virtio SNP/provider readiness, but no physical controller acceptance |
| Audio driver | AAVMF no-device nonfatal | Native ARM64 PE and teardown audit; AAVMF safely continues without compatible audio, and physical playback is unverified |
| Software GLES | Compiles; runtime unverified | No ARM64 rendering acceptance suite |
| GPU acceleration | Unverified | No ARM64 native GPU driver or hardware run |
| Secure Boot | Unverified | No enrolled signing-chain test |

The preview must preserve these labels in its GitHub release and website
catalog. Evidence may be promoted only after a target-specific acceptance run;
x64 results do not establish ARM64 support.
