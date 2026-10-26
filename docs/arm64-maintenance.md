# ARM64 suitcase maintenance

OpenConsole maintains x64 and ARM64 as separate public repositories and release
channels, generated from one governed portable source tree. This is not binary
compatibility: each parent, child, and driver must be compiled for its target.

| Contract | x64 | ARM64 |
| --- | --- | --- |
| Compiler target | `x86_64-unknown-windows` | `aarch64-unknown-windows` |
| PE machine | `0x8664` | `0xAA64` |
| Removable boot path | `EFI/BOOT/BOOTX64.EFI` | `EFI/BOOT/BOOTAA64.EFI` |
| Manifest token | `x64` | `arm64` |
| OCG2 entry role | `entry-x64` | `entry-arm64` |
| VM | QEMU x86_64 + OVMF | QEMU `virt` + AAVMF |

Format framing, signatures, OCJ1 jackets, manifest field meanings, and versioned
OpenConsole service ABIs are shared. `scripts/export-public-repositories.py`
records the engineering source revision and hashes the shared contract files.
Release automation must reject unintended differences in those hashes.

Use `make test-arm64-host` for dual-target ABI and OCG2 tests, then
`make test-arm64-aavmf` for deterministic image, architecture inventory,
dashboard screenshot, input, launch, and return evidence. Promote a capability
only from evidence on the same architecture. In particular, x64 network, audio,
or GPU results do not imply ARM64 support.

The pinned EDK II upper network stack is the first shared source-based driver
portfolio. `make edk2-network-stack` produces X64 modules with GCC and AARCH64
modules with CLANGDWARF from the same pinned revision. Every output is rejected
unless it has the expected PE machine, EFI boot-service-driver subsystem, no
hosted imports, and a zero timestamp. `make arm64-image` packages only the
AARCH64 outputs under `EFI/OpenConsole/Drivers/Network` and records each with
its `network` or supporting `crypto` role in the signed `platform.ocp`; the
deterministic-image test verifies both the signature and every inventoried file
digest.

Security fixes to shared parsing, authentication, launch lifecycle, and public
headers must be exported to both repositories under compatibility-aligned tags.
Architecture-only fixes are labeled in their own repository and backported only
when applicable. Physical-device qualification is tracked separately from VM
evidence.
