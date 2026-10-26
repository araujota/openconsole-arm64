<!-- SPDX-License-Identifier: Apache-2.0 -->
# Hosted development-preview acceptance — 2026-10-25

This record covers service and disposable-OVMF acceptance. It is not physical
hardware, Secure Boot, sandbox, or general GPU compatibility evidence.

## Hosted service

- Convex development deployment: `agreeable-alligator-617`.
- Convex production deployment: `neat-cat-330`.
- Vercel project: `openconsole` (`prj_jVyhOEyeVdcKMYlit5Qo0c55BjaV`).
- Protected preview commit: `b9ae74663ef551b77db1e5bfd78fc29b890097ee`.
- Preview URL: `https://openconsole-49co5nu09-araujota97gmailcoms-projects.vercel.app`.
- Hosted Playwright result: 3 passed (browse/details/download/digest,
  upload/analysis/refresh, extension/accessibility).
- Convex checks: schema and indexes pushed; upload validators, storage records,
  analyzer action, report persistence, reviewer publication, size quota, and
  service logs exercised. The 64 MiB + 1 byte request failed closed with
  `UPLOAD_SIZE_LIMIT`.

No deployment credential or bypass token is recorded in this repository.
Reviewer publication for this development preview uses the explicitly
configured CLI-injected identity; production end-user authentication is not
claimed.

## Catalog-to-firmware path

- Published fixture: `analyzer-fixture` / Signal Runner `1.0.1`.
- Downloaded OCG2 bytes: `1930`.
- OCG2 SHA-256:
  `cdbd827324517914348db61e879b0d4b3807d7efa8d1eba6157f6c11641e29a1`.
- Analyzer report SHA-256:
  `908439a6696d58455f1a1d48a10bcb71f26df936023a0ae3cf40eb04a901eafe`.
- Analyzer/rules versions: `0.1.0-service` / `2026-10-25.1-service`.
- Canonical acceptance FAT SHA-256:
  `b4be2a091c9e4284dcfc678ba1642899005e88052db3667e61ba15a4ed605e64`.

The test independently verified download length and digest, prepared the
authenticated selected slot through the transactional installer, cold-booted
the canonical image, discovered exactly one catalog game, launched its x64 EFI
application, observed child start and return, persisted completion, and
rescanned the library. The fixture is intentionally a minimal launch/return
program, not a gameplay or full GLES demonstration.

## Recovery follow-up

The current firmware then passed these OVMF cases:

- incomplete launch enters baseline safe mode, quarantines the exact bundle,
  and permits explicit retry;
- launch-time authenticated reopen rejects post-discovery mutation;
- a corrupt inventoried optional driver is rejected while baseline UI remains;
- unsupported firmware watchdog behavior remains diagnosable and recoverable;
- a non-returning child followed by reset quarantines the exact bundle on the
  next boot.

Physical Ethernet, audio, AMD acceleration, Secure Boot enrollment, and broad
firmware compatibility remain unsupported claims for this development preview.

## Clean release and production deployment

- Accepted source commit:
  `fe520166ada7c227fb9ebcec47125845e92e050f`.
- Vercel deployment: `dpl_ES4EGi5jzEtistS5eUKrpY8LHgd4`.
- Immutable URL:
  `https://openconsole-bzaijqj4s-araujota97gmailcoms-projects.vercel.app`.
- Production URL:
  `https://openconsole-araujota97gmailcoms-projects.vercel.app`.
- Release FAT SHA-256:
  `4c7a26409dd5c72c29878c3a3a900be744263d43d18f69b227cdf6627cb0883e`.
- Source archive SHA-256:
  `8e1dfcda2c41a8687d58bde8573bf1e2ff277257aea68acc80971277b5221ecf`.

The matrix passed host sanitizers and freestanding checks, deterministic
analyzer corpus/parity, web audit/lint/type/unit/build, local production-build
Playwright, legal/SBOM regeneration, fresh extracted-source verification,
dashboard/GLES/input/network/audio/integrated OVMF suites, nine transactional
interruption states, reproducible development images, and runtime-integrity
recovery cases. The production URL returned unauthenticated HTTP 200 and its
three Playwright stories passed against production Convex, including a real
upload and analyzer execution.

Production is intentionally public so firmware clients can reach the catalog;
Vercel Authentication remains scoped to preview deployments. The CLI assigned
the production alias while creating the production-target deployment despite
`--skip-domain`; acceptance therefore immediately tested the exact immutable
deployment and retained it only after every mandatory gate passed.
