# OpenConsole signed platform inventory (`OCP1`)

`EFI/OpenConsole/platform.ocp` authenticates every parent-controlled image that
the parent may load. It is an inventory, not a container: file bytes remain at
their canonical boot-volume paths. Integers are little-endian and all reserved
bytes are zero.

## Envelope

The file is `128-byte header || 114-byte Ed448 signature || canonical index`.
The header fields are:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | `OCP1` |
| 4 | 2 | major `1` |
| 6 | 2 | minor `0` |
| 8 | 4 | header size `128` |
| 12 | 4 | index length, maximum 256 KiB |
| 16 | 4 | entry count, maximum 256 |
| 20 | 2 | signature algorithm `1` (Ed448) |
| 22 | 2 | signature length `114` |
| 24 | 8 | total file length |
| 32 | 32 | SHA-256 of canonical index; inventory ID |
| 64 | 32 | key ID: SHA-256 of raw Ed448 public key |
| 96 | 32 | reserved |

Signature input is:

```text
"OPENCONSOLE-PLATFORM-SIGNED\0" || header || index
```

The signing key MUST have `platform-inventory` scope and is distinct from
publisher and parent-update keys.

## Canonical index

The index begins with a 64-byte header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | `OPI1` |
| 4 | 2 | major `1` |
| 6 | 2 | minor `0` |
| 8 | 4 | header size `64` |
| 12 | 4 | entry count |
| 16 | 4 | profile byte length |
| 20 | 4 | parent-version byte length |
| 24 | 4 | flags; bit 0 permits visibly unverified local games |
| 28 | 8 | total entry-record bytes |
| 36 | 28 | reserved |

The header is followed by ASCII profile (`development`, `test`, or `release`),
ASCII parent version, then sorted records. Each record is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | path length |
| 2 | 2 | role |
| 4 | 4 | flags; bit 0 means safe-mode baseline |
| 8 | 8 | exact file length |
| 16 | 32 | SHA-256 of file bytes |
| 48 | 2 | component-version length |
| 50 | 2 | key scope (`1` platform, `2` third-party-driver) |
| 52 | path length | canonical volume-relative path |
| ... | version length | ASCII component version |

Paths use `/`, are sorted strictly by raw bytes, are unique under ASCII case
folding, and MUST be beneath `EFI/BOOT/` or `EFI/OpenConsole/`. `platform.ocp`
cannot inventory itself. Roles are parent (`1`), input (`10`), network (`11`),
audio (`12`), crypto (`13`), graphics (`14`), trust (`20`), notices (`30`),
and recovery (`40`). Every executable role is re-opened and verified against
this record immediately before `LoadImage`.

Stable errors use the `OCP1_` prefix and mirror OCG2 categories: magic,
version, header, limit, overflow, length, index digest, trust/signature, path,
order/collision, role, and file digest.
