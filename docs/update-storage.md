# Transactional update storage

OpenConsole never overwrites `\EFI\BOOT\BOOTX64.EFI` while it is running. A
verified candidate is written to the inactive one of two same-volume artifact
paths under `\EFI\OpenConsole\Updates`. The file remains open through UEFI
`Flush()`, is reopened, bounded by the accepted update policy, and hashed through
the parent-loaded CryptoPkg SHA-256 protocol before any selection record changes.

Boot selection is a two-record journal. Each fixed-size `OCS1` record contains a
monotonic generation, artifact slot, exact byte length, SHA-256 digest, the full
accepted version/sequence/key-rotation policy, and CRC32 over the record. The
inactive selector is replaced only after its artifact has passed persisted-byte
verification. Artifact selection and the new rollback/key trust floor therefore
become durable together. Recovery validates both records and their referenced
artifacts, then selects the highest valid generation. Consequently a torn
selector, failed flush, missing file, wrong length, or corrupt artifact cannot
displace the current valid selection; after a successful update, the previous
slot remains the last-known-good candidate.

The host sanitizer test injects write, flush, torn-record, and digest failures.
`make test-update-storage-ovmf` independently uses the real OVMF FAT filesystem,
commits two generations from a child EFI, corrupts the selected artifact, and
recovers the prior generation. The harness hashes `EFI/BOOT/BOOTX64.EFI` before
and after the run and requires byte-for-byte preservation. This establishes the
storage transaction required by task 4.3. `make test-update-e2e-ovmf` additionally
downloads the actual generated parent EFI through the packaged TLS/HTTP stack,
authenticates and commits it, runs the complete rejection matrix, and proves the
active removable-media entry point remains byte-for-byte unchanged. This
completes the task 4.4 composition evidence.
