# Bespoke AMD backend service map and safety boundary

The future backend can preserve the client ABI by implementing the same private
operations as software: target create/destroy, clear/draw submission, and
present. That small table does not make the implementation small; every service
below the table is presently missing.

| Service | Future backend responsibility | Current state |
| --- | --- | --- |
| Device ownership | exact supported PCI IDs, BAR ownership, conflict-free handoff | Missing; GOP/firmware may own device |
| Register access | IP-version-specific BAR bounds and ordered MMIO | Prohibited/read-only discovery only |
| Firmware | provenance, version matching, validation and upload sequence | Missing |
| Memory | VRAM/GTT BO allocation, pinning, cache policy, CPU mapping | Missing |
| GPUVM | page tables, VMIDs, mappings, invalidation | Missing |
| Queues | rings, doorbells, writeback, command validation | Missing |
| Shaders | compiler pipeline and target ISA metadata | Missing |
| Synchronization | fences, waits, timeouts, memory barriers | Missing |
| Fault recovery | hang detection, queue/device reset, bounded cleanup | Missing |
| Power/thermal | clock, voltage, fan and thermal-safe policy | Missing |
| Display ownership | scanout configuration or safe GOP coexistence/handoff | Missing |

`src/gpu/pci_probe.c` calls only `EFI_PCI_IO_PROTOCOL.Pci.Read`. The build and
OVMF tests contain no PCI configuration write, BAR memory access, bus-master
enable, DMA setup, firmware transfer, queue submission, reset, power operation,
or display takeover. QEMU RV100 `1002:5159` is discovery evidence only.

The separate OpenSpec change `gate-physical-amd-bringup` is the required review
boundary before any of those operations. Until it is approved and applied, the
software backend is the only implemented GLES backend.
