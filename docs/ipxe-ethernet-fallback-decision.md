# iPXE Ethernet fallback gate

Decision: **rejected for the OpenConsole baseline**. This is an engineering and
distribution policy decision, not legal advice.

## Technical decision

iPXE is a strong future candidate for broad PCI Ethernet coverage on x64. Its
official build documentation describes `ipxe` as including almost all PCI NIC
drivers and provides an EFI-driver (`.efidrv`) output. That same documentation
explicitly says the aggregate native-driver build does not currently work on
ARM, although ARM64 EFI itself is supported. Therefore it cannot substantiate
the required x64/ARM64 parity.

An iPXE EFI executable is primarily a network boot environment. Before it could
serve OpenConsole it would need a proven, persistent protocol handoff after
returning to the parent, exact-device manifests, bounded controller ownership,
DMA teardown, and unchanged `OC_NET_PROTOCOL` behavior. None of those has been
qualified, so neither `ipxe.efi` nor a native `.efidrv` is a drag-and-drop
baseline provider.

## Licence decision

iPXE states that most of the project is GPL version 2 or later, with some files
under compatible licences. Its official process requires generating a
`.licence` report for the exact build target; an example resolves to GPL version
2 only. Linking that code into the Apache-2.0 OpenConsole core is not approved.

The repository also contains an Unmodified Binary Distribution Licence (UBDL),
but it applies only where the relevant source files grant it and only to an
unmodified binary built from already-public GPL source. It also requires the
program to announce the public source location when run. This is not a blanket
permission for modified, embedded, or arbitrarily configured builds.

## Consequence

- No iPXE code is imported or linked into the OpenConsole core.
- No iPXE binary is included in either suitcase.
- A future separately distributed EFI image would require its own exact-target
  licence report, corresponding-source process, technical handoff design,
  security review, and new OpenSpec change.
- That follow-up change is not authorized by this gate, so the conditional
  proposal task is intentionally not triggered.

Primary sources: [iPXE licensing](https://ipxe.org/licensing),
[official build targets](https://ipxe.org/appnote/buildtargets), and
[COPYING.UBDL](https://github.com/ipxe/ipxe/blob/master/COPYING.UBDL).
