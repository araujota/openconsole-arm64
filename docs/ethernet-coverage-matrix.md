# Baseline Ethernet coverage matrix

The authoritative data is `config/ethernet-coverage-matrix.json`. This document
explains its decisions; it is not a claim that every listed controller works.
The sample establishes engineering priority from current and installed-base
consumer/OEM systems without pretending to be market-share research.

## Immediate baseline

| Family | Why it is baseline | x64 | ARM64 | Bundle status |
| --- | --- | --- | --- | --- |
| Firmware SNP/UNDI | Lowest-risk path when the board firmware publishes a working controller | Use first | Use first | Firmware-owned |
| Realtek RTL8111/8168 | Broad consumer and HP installed base | 2.081 rejected for baseline | 2.081 lifecycle-blocked | x64 contains NVRAM configuration functionality; ARM64 teardown unknown |
| Realtek RTL8125 | Common AM5 and other 2.5 GbE boards | 2.081 rejected for baseline | 2.081 lifecycle-blocked | P0 replacement or isolated-provider target |
| Realtek RTL8126 | Emerging 5 GbE consumer boards | 2.081 rejected for baseline | 2.081 lifecycle-blocked | P1 replacement or isolated-provider target |
| Intel I219/I211 | Dell, HP, Lenovo, and Intel-board installed base | Intel/vendor firmware first | No proven portable driver | LOM caveat blocks generic Intel bundle |
| Intel I225/I226 | Current 2.5 GbE OEM and enthusiast systems | Intel/vendor firmware first | No proven portable driver | Exact variant evidence required |
| Marvell AQC107/AQC113 | Premium workstation/consumer 5/10 GbE | Candidate | Candidate | No admitted package |
| Broadcom NetXtreme | OEM/workstation spillover | Candidate | Candidate | No admitted package |

Realtek's official 2.081 archive has now been acquired and inspected. It contains
signed x64 and ARM64 EFI drivers and admits `10ec` devices `8125`, `8126`,
`8127`, `8136`, `8161`, `8162`, `8168`, and `9151`. It contains no licence or
redistribution grant, so releases cannot bundle it; verified offline admission
would address licensing only. The x64 image also contains a RealManage action
that saves configuration to NVRAM, violating the baseline capability policy;
it is rejected even as a user-acquired runtime provider. The ARM64 image omits
the discovered configuration markers but remains blocked pending physical DMA,
bus-master, failure, and disconnect evidence.

Intel's 31.3 preboot download publishes a SHA-512 for its archive, but Intel
also states that BootUtil is not intended for LAN-on-motherboard devices. The
public page therefore cannot substantiate generic support for the I219/I225/I226
controllers integrated into consumer motherboards. The package remains
user-acquired until its `redist.txt`, supported-device tables, architectures,
and individual EFI images are inspected.

## EDK II boundary

The pinned EDK II revision provides the IP stack and `SnpDxe`, which adapts a
real NII/UNDI provider to SNP. Those modules do not themselves drive onboard
Ethernet silicon: firmware or an admitted PCI hardware provider must own the
controller first.

`SnpDxe.efi` is built, PE-verified, size-gated, signed into the platform
inventory, and packaged for both x64 and ARM64. It is deliberately a wrapper,
not a hardware-driver coverage claim: acceptance still requires the selected
vendor image to publish NII/UNDI for the intended controller and the resulting
SNP instance to pass bounded initialization and packet checks.

At runtime, an already-published firmware SNP always wins. If no SNP exists,
the launcher enumerates at most 16 NII 3.1 controllers, loads the signed
architecture-matched `SnpDxe.efi`, and connects it only to those controllers.
It retains the wrapper only for the controller that actually publishes SNP;
unsuccessful bindings are disconnected and a wholly unsuccessful attempt is
unloaded. The normal SNP backend is then reprobed, so games continue to see the
same `OC_NET_PROTOCOL` regardless of whether SNP came directly from firmware or
from an admitted UNDI provider through the wrapper.

The release gate limits the complete source-built upper stack to 2 MiB per
architecture, any single module to 1.25 MiB, and the portfolio to 4% of the
64-MiB deterministic suitcase. The current exact measurements are emitted to
`build/edk2-network-stack/SIZE-BUDGET.json`; exceeding any limit fails the
network-stack build rather than silently growing the baseline.

## Firmware divergence is a matrix dimension

A compatibility row is keyed by architecture, board model and revision,
firmware vendor and version, Network Stack/PXE settings, exact PCI identity and
revision, and driver version. Firmware-only inventory is captured before any
OpenConsole driver loads. Post-driver inventory is stored separately.

The same board may therefore have separate rows for Network Stack disabled,
IPv4 PXE enabled, or a different BIOS release. A menu path is setup guidance,
not evidence. A supported row requires initialization, DHCP, DNS, authenticated
HTTPS, an externally observed packet exchange, and clean teardown on the exact
configuration.

Operator-facing, vendor-family navigation hints are published in
[`ethernet-firmware-setup.md`](ethernet-firmware-setup.md). Missing or renamed
menus never change a row's evidence tier without a new captured run.

## ARM64 is not x64 with a different executable

The ARM64 sample now includes three deliberately different platform shapes:

- ASRock Rack's Ampere Altra board is the closest conventional UEFI
  motherboard analogue. It uses PCI-attached Intel I210 and X550 controllers,
  so exact PCI inventory and firmware SNP testing can follow the same model as
  x64 once hardware is available.
- Raspberry Pi 5 exposes integrated Gigabit Ethernet through an SoC MAC. Its
  official product brief does not establish a vendor UEFI/SNP contract, so it
  is recorded as unsupported rather than treated as a generic PCI-driver
  target.
- Jetson AGX Orin uses a UEFI bootloader, but its Gigabit path is an SoC RGMII
  MAC plus carrier-board PHY. It therefore requires firmware SNP evidence or a
  separately reviewed platform provider; a Realtek or Intel PCI catalog entry
  cannot cover it.

This split is represented directly in the machine-readable connection-type
matrix. Firmware SNP is the common baseline on both architectures. PCI LOM
drivers can be portable across architectures only when an architecture-native
binary exists and the exact PCI tuple is admitted. SoC MAC providers are tied
to the board support package and cannot be inferred from link speed or RJ45
presence.

## Current physical observation

The available MSI MAG X670E TOMAHAWK WIFI host reports BIOS `1.G0`, board
revision `1.0`, and RTL8125 `10ec:8125` revision `05`, subsystem `1462:7e12`.
This is host inventory only. Its firmware Network Stack/PXE state and UEFI
SNP/UNDI behavior remain unobserved until a physical UEFI evidence run is made.

## Architecture-indexed exact-identity status

Every fully observed PCI identity has separate x64 and ARM64 records across six
non-interchangeable axes: source build, VM execution, exact catalog match,
representative physical validation, delivery class, and final support state.
The current `10ec:8125 / 1462:7e12 / rev 05` observation remains
`inventory-only`; ARM64 remains `not-tested` and has no exact platform tuple.
The audited Realtek artifact is retained as a non-production parser/selector
fixture, but the production catalogs remain empty until DMA and bus-master
teardown are proven. It is not a bundled provider or support claim. The
validator rejects missing architecture rows and forbids a
`supported` state without physical validation.

The signed platform inventory now authenticates a compact `OEC1` runtime
catalog at `EFI/OpenConsole/Drivers/Network/catalog.oec`. Each fixed-size entry
contains architecture, exact PCI/subsystem/revision bounds, expected protocols,
artifact size and SHA-256, priority, identifier, and bounded EFI path. Both
production catalogs are currently empty; the ARM64 catalog additionally awaits
an exact ARM64 host identity. The catalog never authorizes a
vendor-only match and does not make the proprietary binary redistributable.

## Bootstrap rule

Critical Ethernet drivers must be present in the suitcase or installed offline
from a verified artifact. Downloading a missing Ethernet driver over Ethernet
is not a valid bootstrap design. Online updates may replace a working provider
later, but are not part of baseline connection establishment.
