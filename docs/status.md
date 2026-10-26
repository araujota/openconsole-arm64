<!-- SPDX-License-Identifier: Apache-2.0 -->
# Project status

Status date: 2026-10-25. Evidence labels are deliberately separate.

| Area | Host evidence | OVMF evidence | Physical evidence | Release state |
| --- | --- | --- | --- | --- |
| OCG2, manifest v2, OCJ1, controls | Deterministic golden/malformed/migration and cross-language tests | Authenticated selected slot parsed by parent | None | Development format frozen; pre-1.0 |
| Transactional game state | Sanitized two-slot install/delete/recovery and fault matrix | Signed slot discovery, hosted-catalog install, mutation rejection, tombstone/recovery flows | None | OVMF acceptance pass |
| Runtime integrity | SHA-256, Ed448, OCP1/OCPK, PE and authenticated-load tests | Pinned crypto bootstrap; platform, driver, trust, and game verification | None | Development enforcement active |
| Child recovery | Checksummed attempt/completion/quarantine tests | Watchdog request, incomplete-attempt quarantine, baseline safe mode, explicit retry, successful return | None | OVMF pass; hostile containment is not claimed |
| Dashboard/input | Layout, search, pointer, pagination, modal tests | GOP screenshots, USB mouse, keyboard handoff | None | OVMF pass |
| Network | Provider/backend/failure tests | Packaged EDK II stack over virtio SNP, DHCP/DNS/UDP/HTTP/TLS fixtures | Inventory workflow only | VM-qualified only |
| Audio | Mixer golden WAV, AudioIo adapter tests | QEMU HDA/AudioDxe capture and cleanup | Inventory workflow only | VM-qualified only |
| GLES | ABI/frontend/software rasterizer tests | GOP software triangle/reference scene | No accelerated AMD path | Narrow nonconformant reference subset |
| Analyzer | Capstone corpus, fuzz seeds, deterministic JSON/text/SARIF | Not a firmware component | N/A | Host and Convex pre-execution stage parity passes |
| Catalog | Production Convex upload/analyze/publish/download and exact-deployment Playwright tests | Published bundle digest verified, installed, launched, returned, and recovered | N/A | Public development preview deployed |

The current artifact is a development preview for disposable OVMF use. EFI
children remain trusted native code. Secure Boot is not enrolled. QEMU
`ati-vga` is PCI-discovery evidence only and says nothing about Radeonsi,
AMDGPU, RDNA2, or Navi 22 execution.

The development-preview release matrix is complete. This does not widen the
explicitly unsupported physical-hardware, Secure Boot, or acceleration claims.
