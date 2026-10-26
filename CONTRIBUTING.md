<!-- SPDX-License-Identifier: Apache-2.0 -->
# Contributing

Submit small changes with an explicit evidence level: host unit, OVMF integration, or named physical hardware. Never promote VM evidence to a physical-support claim. Format/security changes require coherent OpenSpec artifacts, malformed/boundary tests, and stable diagnostics.

Run the relevant `make test-host`, `make test-ovmf`, deterministic image double-build, and web checks from `README.md`. Never commit private keys, `.env` files, Convex credentials, test CA private material, variable stores, or proprietary firmware blobs.

New dependencies need a pin, upstream provenance, license review, notice, and SBOM entry. Participation is governed by `CODE_OF_CONDUCT.md`.

Before submitting a change:

1. Keep format and trust-boundary changes coherent with the active OpenSpec.
2. Add positive, malformed, boundary, and fault tests proportional to risk.
3. Run `make test-host check-efi-core` and, for analyzer changes, `make test-analyzer ANALYZER_PYTHON=build/analyzer-venv/bin/python`.
4. Run `npm run lint`, `npm run typecheck`, `npm run test:unit`, and the relevant Playwright flow from `web/` for client or Convex changes.
5. Regenerate notices, provenance, and the SBOM after dependency, vendor, or patch changes.

Contributions are licensed under Apache-2.0 unless a clearly identified third-party file has a different compatible license.
