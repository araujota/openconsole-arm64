# Capstone dependency record

- Component: Capstone disassembly framework
- Pinned version: 5.0.6
- Upstream: https://github.com/capstone-engine/capstone/tree/5.0.6
- License: BSD-3-Clause-compatible Capstone license; exact upstream text is in
  `THIRD_PARTY_LICENSES/Capstone.txt`.
- Scope: host/service analyzer only; it is not linked into the parent or child
  EFI binaries.
- Purpose: bounded instruction-boundary decoding of x86-64 executable PE/COFF
  sections. OpenConsole owns policy, budgets, reports, and findings.
- Review boundary: skip-data regions are counted and force an inconclusive
  verdict above the published threshold. Capstone output does not by itself
  prove reachability or safety.
- Update policy: pin exact versions, run the adversarial corpus and runtime
  budget suite, inspect upstream security notices, refresh notices/SBOM, and
  record the reviewed source revision before changing this pin.
