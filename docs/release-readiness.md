<!-- SPDX-License-Identifier: Apache-2.0 -->
# Development-release readiness

Status: **ready for a clearly labeled, disposable-VM developer preview; not
ready for promotion as a general hardware release**. Format, analyzer, local catalog, signed
runtime loading, transactional state, watchdog quarantine, and recovery are
implemented. Remaining gates are explicit rather than inferred from passing
subsets.

## Standardization assessment

The on-media contracts are strong enough to freeze for a development release:

- A distributable game is one deterministic, signed `OCG2` file. Its canonical
  index assigns roles to normalized paths; `game.toml` manifest v2 identifies
  the game, version, architecture, entry image, capability versions, graphics
  policy, metadata, and requirements. Jackets use bounded raw-pixel `OCJ1` and
  controls use a bounded declared grammar. Independent C, Zig, and Rust ABI
  examples exercise the same contracts.
- Installed games do not invent their own directory layout. The parent owns
  `EFI/OpenConsole/State/Games/<id>/`, retains two versioned slots plus two
  selector records, and exposes a package-root handle to the selected child.
  Direct loose-tree discovery remains a legacy/test-profile facility rather
  than the canonical development path.
- The parent image has one builder and one documented FAT tree. Boot code,
  signed `platform.ocp`, publisher keys, baseline drivers, optional drivers,
  transactional state, update state, logs, and profile boundaries have reserved
  locations. Inputs are inventoried and builds are byte-reproducible.

The principal compatibility risk is no longer an ambiguous game/tree format.
It is incomplete breadth: the GLES2 surface and AMD acceleration path are not
yet complete, physical firmware/device coverage is intentionally unclaimed,
and format evolution has not yet accumulated third-party implementation
experience. Version negotiation and required-versus-optional capabilities
contain that risk without silently accepting unsupported behavior.

## Completed foundations

- Canonical deterministic OCG2, manifest v2, capability registry, OCJ1 and
  controls formats, minimal portable examples, and Ed448 publisher identity.
- Canonical parent FAT layout with pinned OCP1 trust, inventoried baseline
  drivers, deterministic image construction, and profile separation.
- Two-slot installed games with retained bundles/descriptors, reread/digest
  validation, selectors, tombstones, bounded cleanup, and canonical-only
  discovery in the pinned development parent.
- Immediate pre-load authentication for drivers and games, real selected-slot
  device paths, launch records, watchdog requests, exact-bundle quarantine,
  baseline safe mode, visible inspect/delete access, and explicit retry.
- Bounded host and Convex analyzers with parity for container, trust/signature,
  manifest/assets, installed-tree projection, and compatibility.
- Local Convex/Next upload, analysis, reviewer publication, catalog, download,
  accessibility, and error-flow tests.
- Apache-2.0 project licensing, Contributor Covenant, security/threat policy,
  deterministic third-party notices, patch provenance, and CycloneDX SBOM.

## Hosted acceptance completed

The intended Convex and Vercel projects are linked with separate development,
preview, and production configuration. The linked Convex deployment passed a
real upload, bounded analysis, reviewer publication, storage/download, quota,
and digest check. The protected Vercel preview for the recorded commit passed
health, catalog, accessibility, upload/refresh/analysis, download, and digest
tests. Its published bundle was then downloaded, independently hashed,
transactionally installed into the canonical FAT image, launched and returned
under OVMF. Cold-boot quarantine, manual retry, mutation, corrupt-driver,
unsupported-watchdog, and hang/reset cases passed afterward.

The clean matrix passed against source commit
`fe520166ada7c227fb9ebcec47125845e92e050f`. Vercel production deployment
`dpl_ES4EGi5jzEtistS5eUKrpY8LHgd4` records that exact commit in deployment
metadata and passed public health plus the complete hosted browser suite against
production Convex before acceptance. The production alias points at that same
deployment. The release FAT digest is
`4c7a26409dd5c72c29878c3a3a900be744263d43d18f69b227cdf6627cb0883e`.

The repository now contains pinned CI and deterministic release-candidate
builders. Local evidence includes a byte-identical two-image comparison,
secret/profile inspection, checksum verification, and host/freestanding tests
from a fresh extracted source archive; this does not substitute for the hosted
CI and deployment gates above.

The first permissible label is **development preview for disposable OVMF
testing**. It must state that analysis is not a sandbox or safety proof, Secure
Boot is unenrolled, GLES coverage is incomplete, AMD acceleration is
experimental, and physical compatibility is limited to separately recorded
hardware evidence.

Before accepting untrusted public submissions on real hardware, retain passing
evidence for parser fuzz/sanitizer budgets, PE/COFF role checks, signature and
revocation policy, mutation-at-load rejection, path/case collision rejection,
transaction interruption recovery, exact-target deletion, resource cleanup on
both normal and error returns, watchdog/quarantine behavior, network/TLS bounds,
and analyzer inconclusive/fail-closed outcomes. The release process must also
prove the published source archive and FAT digest reproduce from a clean host,
contain no keys or undeclared generated files, and exactly match the deployed
catalog/analyzer revision.
