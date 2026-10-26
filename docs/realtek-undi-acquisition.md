# Realtek UEFI UNDI 2.081 audit

The official download flow was completed through its published ALTCHA SHA-256
proof-of-work protocol. `scripts/acquire-realtek-undi.py` reproduces the download
without embedding an expiring token. The official 319,890-byte archive has
SHA-256 `4c4ce542c4aefe4b653e5c7490739e1e942703fe49bd8e0ca2ab80662cc14464`.

It contains x86, x64, ARM64, and LoongArch64 EFI boot-service drivers. The x64
and ARM64 images are 201,616 and 168,336 bytes. Both carry an Authenticode chain
from `Microsoft Option ROM UEFI Signer` to `Microsoft Option ROM UEFI CA 2023`.
That signature does not grant redistribution or constitute OpenConsole approval.

The x64 Driver Binding `Supported()` routine admits Ethernet-class functions
for PCI vendor `10ec` and device IDs `8125`, `8126`, `8127`, `8136`, `8161`,
`8162`, `8168`, and `9151`, without subsystem or revision filtering. Therefore
OpenConsole's signed manifest must apply exact subsystem/revision policy first.

The ZIP contains no licence, readme, source, supported-ID table, or
redistribution grant. It is classified **user-acquired**, not bundled. Its
binaries are not committed or copied into release images. Physical packet, DMA,
teardown, and exact-device qualification remain required before support claims.

The architecture binaries are not capability-equivalent. Reproducible static
inspection of the pinned x64 image finds a RealManage configuration interface,
including `Save RealManage Configuration to NvRam` at file offset 154201 and a
reset-required warning. Because OpenConsole cannot remove or interpose this
proprietary persistent-mutation path, x64 2.081 is rejected as a baseline
provider. The ARM64 image lacks those discovered markers; that is only negative
static evidence, not proof of safe behavior, so ARM64 remains lifecycle-blocked.
The evidence record is `config/realtek-undi-2.081-static-lifecycle.json` and the
digest-gated reproducer is `scripts/audit-realtek-lifecycle.py`.

Primary source: [Realtek PCIe Ethernet downloads](https://www.realtek.com/Download/List?cate_id=584).
