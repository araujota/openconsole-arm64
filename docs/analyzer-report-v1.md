# OpenConsole analyzer report v1

Status: normative for the development analyzer. The analyzer detects risk
indicators; it is not a sandbox, proof of safety, or substitute for publisher
review. An accepted EFI application still executes as trusted native firmware
code in the parent's address space.

## Determinism and inputs

The normalized report schema is `openconsole.analyzer.report.v1`. Given the
same analyzer version, rule-set version, trust snapshot, policy, and bundle
bytes, all report fields and the report digest are byte-for-byte stable. Wall
clock time, host paths, temporary paths, and exception text are excluded.

The report digest is SHA-256 over compact UTF-8 JSON with sorted keys and the
`reportDigest` member omitted. Findings are sorted by rule ID, file, file
offset, virtual address, and evidence hash. At most 256 findings are emitted.

## Budgets

| Resource | Development limit |
| --- | ---: |
| Bundle bytes | 64 MiB |
| Files | 256 |
| Individual file | 32 MiB |
| Executable sections | 32 |
| Decoded/scanned executable bytes | 8 MiB |
| Findings | 256 |
| Analysis CPU deadline | 5 seconds |

Crossing a byte/count/output budget produces `inconclusive`; malformed input,
failed digests, invalid PE structure, or a blocking policy finding produces
`rejected`. An expired CPU deadline is reported as `ANALYZER_BUDGET_TIMEOUT`
without embedding measured timing in normalized output.

## Finding contract

Each finding has a stable `ruleId`, `severity` (`info`, `low`, `medium`,
`high`, or `critical`), `confidence` (`verified`, `high`, `medium`, or `low`),
`publicationEffect` (`none`, `review`, `block`, or `inconclusive`), package
relative `file`, nullable `fileOffset` and `virtualAddress`, an SHA-256
`evidenceHash`, explanation, and remediation. Evidence hashes cover no more
than 32 bytes around the indicated evidence; raw machine code is not included
in service reports.

Verdicts are `accepted`, `rejected`, or `inconclusive`. `block` takes
precedence over `inconclusive`; either prevents automatic publication.
Signature verification establishes publisher identity only, never code
correctness. A structurally valid signature without a trusted key produces an
inconclusive report.

## Output forms

JSON is authoritative. Text is a stable human rendering. SARIF-compatible
output uses SARIF 2.1.0 result locations and stores confidence, publication
effect, evidence hash, and verdict in properties. Conversion never changes
the normalized report digest.
