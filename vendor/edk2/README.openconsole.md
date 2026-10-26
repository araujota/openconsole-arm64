# OpenConsole EDK II network-stack package

- Upstream: `https://github.com/tianocore/edk2.git`
- Pin: `0a503bc2f5bd37c7a5d39d0ba31008f446f0298d`
- License: BSD-2-Clause-Patent for the selected EDK II files, with file-level
  SPDX notices authoritative; bundled OpenSSL is Apache-2.0 and its notices are
  authoritative.
- Compatibility: X64 UEFI 2.x systems which already expose a usable SNP handle.
  This package contains no SNP, UNDI, PCI, or NIC hardware driver.
- Local patches: the exact patch set under `patches/edk2/` is applied only by
  the target-specific build scripts. Every patch digest and target is recorded
  in `dependency-provenance.json`; OpenConsole's loader remains under `src/net/`.

`scripts/build-edk2-network-stack.sh` checks out the immutable pin, initializes
only the pinned build submodules, builds the ten modules in dependency order, and
emits binaries plus `SHA256SUMS` beneath `build/edk2-network-stack/`.

The runtime package is DpcDxe, MnpDxe, ArpDxe, Dhcp4Dxe, Ip4Dxe, Udp4Dxe,
TcpDxe, DnsDxe, TlsDxe, and HttpDxe. `make esp-network-stack` places that exact
set under `EFI/OpenConsole/Drivers/Network/`. The loader first checks each
published protocol and never loads a module whose protocol is already present.

The release-wide dependency and license inventory is generated as
`THIRD_PARTY_NOTICES.md` and `sbom.cdx.json`; file-level upstream SPDX notices
remain authoritative.
