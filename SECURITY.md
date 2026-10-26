<!-- SPDX-License-Identifier: Apache-2.0 -->
# Security policy

OpenConsole has no stable supported release. Security fixes apply to the current development branch only; development images are replaceable test artifacts.

Do not publicly report a suspected vulnerability, signing-key leak, or analyzer bypass. Use the repository host's private security-advisory facility (the repository **Security → Advisories → Report a vulnerability** flow when available). If that facility is unavailable, privately contact a listed maintainer before opening an issue. Include the affected commit/artifact digest, reproduction, impact, and least sensitive proof needed. Never attach private keys or unrelated game content. A maintainer should acknowledge receipt within seven days, but this volunteer preview offers no remediation-time SLA.

EFI games and drivers are trusted native code, not sandboxed processes. They may access devices, firmware services, storage, memory, and networking. Signatures establish identity and byte integrity; bounded static analysis can have false negatives. Until every runtime-integrity gate in the active OpenSpec is complete, use disposable OVMF environments only. No claim is made for malicious-code containment, Secure Boot enrollment, or unrecorded physical compatibility.

In scope are package parsers, trust decisions, transactional storage, parent and driver integrity, launch recovery, protocol isolation, catalog authorization, upload handling, and project-code secret disclosure. Unsupported physical hardware behavior, upstream firmware bugs without OpenConsole impact, social engineering, and denial of service against local fixtures are normally out of scope. See `docs/threat-model.md` for the complete boundary and safe reproduction guidance.
