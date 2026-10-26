# Network and audio source/patch policy

The immutable inputs are listed in `io-sources.lock.toml`. A source appearing in
that file is not automatically approved for redistribution or runtime linkage.

## Inclusion rules

1. Copy only files named by an approved bill of materials. Preserve each file's
   copyright, SPDX identifier, and license notice.
2. Put unmodified upstream files beneath `vendor/<project>/<commit>/`. Keep local
   adaptations as reviewable patches or OpenConsole-owned adapter files; never
   silently edit a vendored snapshot.
3. Regeneration must begin from the recorded commit and produce byte-identical
   generated sources or binaries under the documented toolchain.
4. Every EFI binary gets a file-level source/license closure. Aggregate project
   licenses are hints, not substitutes for inspecting linked files.
5. GPL inputs such as Linux r8169 and the general iPXE tree are reference-only
   unless an explicit redistribution/source-delivery decision is reviewed.
6. Hardware facts learned from a reference are recorded with source path and
   revision. No claim of RTL8125 support may be inferred from RTL8168/8169 IDs.
7. Security updates change the pin through a dedicated review containing upstream
   diff, license delta, reproducible build, ABI tests, and VM/physical regressions.

## Initial file-level findings

- EDK II `License.txt` is BSD-2-Clause-Patent; NetworkPkg and dependencies still
  require a generated file manifest before packaging.
- OpenCore `Staging/AudioDxe/AudioDxe.inf` and AudioIo header carry the MIT grant.
  Its library closure is not yet approved wholesale and must be minimized.
- iPXE states that builds are generally GPL and provides a per-image license
  report mechanism. It is not selected as the RTL8125 runtime driver.
- The Linux r8169 sources are GPL-2.0-only and are architecture references only.
- UEFI and Intel HDA publications are specifications, not source-code licenses.
- Realtek's official controller download page lists UEFI UNDI driver `2.081`
  (download ID `3413`) for x64 and explicitly places RTL8125 variants in the
  applicable 2.5-GbE family. The download is CAPTCHA-mediated and the listing
  publishes no redistribution license or artifact digest. It is therefore an
  exact-device candidate, not a vendored dependency. Do not automate around the
  CAPTCHA, mirror the binary, or place it in release media until its package
  terms and file-level provenance have been reviewed.

## External binary acceptance

If the physical inventory selects the no-SNP branch, an operator may obtain the
Realtek package through the vendor's normal download flow for local evaluation.
Before it enters an OpenConsole test ESP, the dedicated RTL8125 change must:

1. retain the original archive, vendor documentation, filename, version, size,
   SHA-256 digest, and Authenticode/PE signature information;
2. extract only the x64 UEFI driver and confirm it is a PE32+ EFI boot-service
   driver rather than a PXE ROM, OS driver, or another architecture;
3. record package terms separately from the web listing and decide whether the
   binary may be redistributed or must remain a user-supplied local input;
4. prove the driver binds only the reviewed `10ec:8125` revision-05 controller,
   publishes standard UNDI/SNP, and leaves unrelated controllers untouched;
5. load it in a dedicated pre-SNP stage, connect only the exact revision-05 PCI
   handle, verify same-device-path SNP publication, and only then initialize the
   packaged EDK II upper stack;
6. validate failure teardown plus repeated link/DHCP/DNS/HTTPS traffic with an
   independent LAN observer.

Run the structural admission check on the extracted x64 image before it is ever
placed on test media:

```sh
./scripts/inspect-uefi-driver.py RtkUndiDxe.efi \
  --expect-sha256 REVIEWED_ARCHIVE_EXTRACT_HASH \
  --output build/realtek-undi-inspection.json
```

This validates the PE architecture, EFI driver subsystem, imports, relocations,
section bounds, and reviewed digest. It deliberately cannot prove PCI matching,
protocol publication, DMA safety, or physical traffic; those remain boot-time
and external-observation gates.

Discovery of this candidate does not complete task 7.3 and is not evidence that
the current firmware or binary supports the named board.

## Local patch layout

Each imported component will have `vendor/<project>/README.openconsole.md` naming
the pin, copied files, build command, hashes, licenses, compatibility allowlist,
and ordered patch list. OpenConsole adapters live outside the vendor directory.
Any exception requires an OpenSpec design update before code is imported.
