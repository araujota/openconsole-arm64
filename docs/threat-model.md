<!-- SPDX-License-Identifier: Apache-2.0 -->
# OpenConsole development threat model

## Assets and trust roots

Protected assets are the parent EFI and baseline drivers, platform and publisher trust stores, installed game selectors and bytes, launch/recovery records, catalog publication state, uploaded artifacts, reviewer authority, and deployment credentials. Private signing keys are offline inputs and must never appear in an image or repository. The parent pins platform trust; bundles and catalog responses cannot introduce trust roots.

## Adversaries and inputs

The design assumes malformed or hostile bundles, interrupted FAT writes, corrupt selectors, replaced drivers, compromised or revoked publishers, anonymous upload abuse, misleading compatibility declarations, and EFI children that hang, reset, leak resources, or invoke sensitive firmware services. Network responses, manifests, assets, PE/COFF bytes, device metadata, and persistent state are untrusted until bounded validation succeeds.

## Security boundaries

- Canonical parsing, length/path confinement, signatures, and current trust authorize installation; adjacent checksum files do not.
- Two slots, redundant records, reread/digest verification, and tombstone-first deletion address interruption and corruption.
- Every driver and game is reopened and authenticated immediately before `LoadImage`.
- Attempt/completion records, watchdog requests, exact-bundle quarantine, and baseline-only recovery address hangs and resets.
- Analysis and publication review are defense in depth. An accepted result is not a safety proof.
- Convex functions enforce validated transitions, quotas, provenance, and reviewer-only publication; browser downloads verify catalog digests.

## Explicit non-goals and residual risk

UEFI provides no process, memory, DMA, filesystem, device, or firmware-service isolation between parent and child. A launched game is trusted native code and can evade analysis, corrupt firmware state, attack devices, or prevent the parent from regaining control. A watchdog may be absent or ineffective. Signatures identify bytes and publishers; they do not make code benign. Secure Boot enrollment and a production key ceremony are outside this preview. QEMU evidence does not qualify physical hardware.

## Safe research and reproduction

Use disposable OVMF variable/FAT images, no personal firmware variables, no production keys, and no sensitive network credentials. Preserve exact artifact digests and bounded logs. Do not run suspected malicious EFI code on physical hardware. Report integrity bypasses privately according to `SECURITY.md` and minimize proof material.
