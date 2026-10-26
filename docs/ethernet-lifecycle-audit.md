# Ethernet driver lifecycle audit

The machine-readable decision is `config/ethernet-lifecycle-audit.json`. An
unknown result is release-blocking; passing `SnpDxe` does not approve the UNDI
hardware provider beneath it.

The pinned EDK II `SnpDxe` uses `EFI_PCI_IO_PROTOCOL.Map/Unmap` callbacks with a
bounded map table, allocates command and transmit/receive memory through PCI I/O,
and issues UNDI shutdown then stop before freeing its wrapper state. Its SNP
`NvData` implementation rejects write requests, so OpenConsole cannot use that
surface to alter NIC NVM. Failed launcher bindings call `DisconnectController`
before `UnloadImage`, and failure to publish SNP leaves the launcher offline.

That proves only the wrapper boundary. A proprietary UNDI image owns controller
registers, interrupt sources, descriptor rings, bus-master enablement, and any
private mappings. Realtek 2.081 and Intel 31.3 therefore remain blocked from the
baseline until source review or instrumented physical tests prove that every
failure and disconnect path quiesces transfers, releases mappings, disables bus
mastering, avoids flash/option-ROM writes, and survives repeated load/unload.

Static inspection strengthens the x64 Realtek decision from “unknown” to
“rejected for the baseline.” The digest-pinned 2.081 x64 image contains a
firmware configuration UI and the literal action `Save RealManage Configuration
to NvRam`, plus `Warning Reset Required`. OpenConsole cannot remove or policy-
interpose that proprietary path, so the complete image violates the baseline's
no-persistent-mutation capability boundary even if ordinary UNDI startup never
invokes it. The smaller ARM64 image does not contain those markers, but marker
absence is not proof; its persistent-write behavior and DMA teardown remain
unknown. `scripts/audit-realtek-lifecycle.py` reproduces this architecture-
specific scan while requiring the previously audited image digests.

Physical qualification must snapshot PCI command/status before load, after
initialization, after forced failure, and after disconnect; observe DMA and
interrupt quiescence; repeat packet exchange and teardown; and verify that NIC
NVM/option-ROM configuration is unchanged. A successful DHCP request is not
teardown evidence.
