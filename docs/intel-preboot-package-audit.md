# Intel Preboot 31.3 Ethernet package audit

The official 3,436,980-byte `Preboot.tar.gz` was inspected on 2026-10-26.
Its SHA-512 exactly matches Intel's published value. Full machine-readable
results are in `config/intel-preboot-31.3-audit.json`.

## Result

The package is **not admitted for baseline redistribution**. It is retained as
a potential user-acquired input only, and even that path needs a lawful method
to extract a specific EFI image plus an exact supported-device table.

The archive contains no loose Ethernet EFI driver. It contains a proprietary
2,167,653-byte `BootIMG.FLB` option-ROM container. BootUtil reports these UEFI
payloads:

| Payload | Version | Architecture | Consumer-baseline use |
| --- | --- | --- | --- |
| PCI-E gigabit | 9.9.25 | x64 | Potentially relevant |
| 10 gigabit | 8.3.65 | x64 | Not baseline consumer LOM coverage |
| 40 gigabit | 5.0.85 | x64 | Not baseline consumer LOM coverage |
| 10 gigabit | 8.3.65 | ARM64 | Not baseline I219/I225/I226 coverage |
| 800 Series | 4.4.62 | ARM64 | Not baseline I219/I225/I226 coverage |

The package readme names I210, I219, I225, and I226, but not I211. This is a
family list, not the exact PCI/subsystem/revision table required by OpenConsole.
The container exposes UNDI evidence, but NII/UNDI publication and the resulting
SNP binding still require runtime verification.

## Motherboard limitation

Intel explicitly says BootUtil is not intended for LAN-on-motherboard use and
that the UEFI option-ROM programming route applies to add-in adapters. We also
ban flash/NVM mutation. Therefore the archive cannot be generalized to the
I219/I225/I226 controllers integrated into sampled consumer motherboards.

## Distribution decision

The included limited-distribution agreement is tied to a product containing an
Intel component and describes the licence as non-transferable and revocable;
it also contains corporate-NDA language. OpenConsole has no approval to
redistribute the archive or extracted payloads. The package supplies no
detached archive signature; the published SHA-512 authenticates the download
only to the extent that the HTTPS Intel page is trusted.

Primary sources: [Intel package page](https://www.intel.com/content/www/us/en/download/15755/intel-ethernet-connections-boot-utility-preboot-images-and-efi-drivers.html)
and [Intel's LOM warning](https://www.intel.com/content/www/us/en/support/articles/000005790/software/manageability-products.html).
