# EDK II packaged network-stack audit

OpenConsole packages twelve EFI boot-service drivers from pinned EDK II commit
`0a503bc2f5bd37c7a5d39d0ba31008f446f0298d`. The closure is DPC, MNP, ARP,
IPv4, UDP4, DHCP4, Hash2, TCP4, DNS4, TLS, HTTP Utilities, and HTTP. Hash2 is
required by the current EDK II TCP implementation and is built through the
locally isolated `0002-networkpkg-hash2-component.patch`. HTTP Utilities is a
runtime protocol dependency of HttpDxe request construction.

`make audit-edk2-network-stack` builds an SNP-only OVMF variant, packages the
drivers on the test ESP, and boots the normal parent and child network probe.
The harness requires `OPENCONSOLE_NET_STACK_PACKAGED_LOADED`, two complete child
generations, externally observed fixture requests, latency markers, and zero
provider allocations after both returns. The SNP-only firmware also attaches a
virtio RNG device because current EDK II IP/UDP/TCP entry points require the UEFI
RNG protocol during initialization.

The resulting `build/edk2-network-stack-audit.json` records:

- empty PE import directories and zero undefined symbols for every EFI;
- individual and total binary sizes and SHA-256 digests;
- the exact linked source/header closure, file digests, and license basis;
- per-operation latency, peak OpenConsole allocations, and teardown balances;
- hashes for the runtime debug log and SNP-only OVMF code image.

The passing reference run contains 12 drivers totaling 1,773,568 bytes and
1,615 unique linked files. It records peak provider allocations of 80 bytes for
each launch and zero live bytes/allocations after both child returns. This is VM
evidence only; it does not establish physical RTL8125 support.
