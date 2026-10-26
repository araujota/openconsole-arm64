# OpenConsole Game Bundle 2 (`OCG2`)

Status: normative development format 2.0. Multi-byte integers are unsigned
little-endian. All offsets are from the start of the region that contains them.
All additions MUST be checked before they are performed. Producers MUST emit
the canonical form; consumers MUST reject noncanonical input rather than repair
it.

`OCG2` is deliberately uncompressed. Authoring tools may accept other archive
formats, but firmware, the catalog, and the analyzer exchange only these bytes.

## Global limits

| Item | Limit |
| --- | ---: |
| Bundle bytes | 64 MiB |
| Index bytes | 256 KiB |
| Files | 256 |
| Path bytes | 240 |
| Game ID bytes | 64 |
| Version bytes | 32 |
| Individual file | 32 MiB |
| Signature | exactly 114 bytes |

## Envelope

The bundle is `header || signature || index || payload`. The header is exactly
128 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OCG2` |
| 4 | 2 | major version, `2` |
| 6 | 2 | minor version, `0` |
| 8 | 4 | header size, `128` |
| 12 | 4 | flags, zero in 2.0 |
| 16 | 8 | canonical index length |
| 24 | 8 | payload length |
| 32 | 4 | file count |
| 36 | 2 | signature algorithm, `1` = Ed448 |
| 38 | 2 | signature length, `114` |
| 40 | 8 | total bundle length |
| 48 | 32 | SHA-256 of the canonical index; also the bundle ID |
| 80 | 32 | publisher key ID: SHA-256 of the raw 57-byte Ed448 public key |
| 112 | 16 | reserved, all zero |

The index starts at `128 + signature_length`. The payload starts immediately
after the index. The declared total MUST equal the input size; trailing bytes
are forbidden.

The signature input is:

```text
"OPENCONSOLE-OCG2-SIGNED\0" || header || index
```

The signature bytes in the envelope are not part of that input. Consumers MUST
verify the header/index digest before invoking Ed448 and MUST match the key ID
to an explicitly trusted publisher key with `game-package` scope. Transport TLS
does not replace this signature.

## Canonical index

The index begins with an 80-byte header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OCI2` |
| 4 | 2 | major version, `2` |
| 6 | 2 | minor version, `0` |
| 8 | 4 | index header size, `80` |
| 12 | 4 | entry count; equals envelope file count |
| 16 | 4 | zero-based manifest entry index |
| 20 | 4 | game ID byte length |
| 24 | 4 | game version byte length |
| 28 | 4 | flags, zero in 2.0 |
| 32 | 8 | sum of entry-record bytes after strings |
| 40 | 8 | payload length; equals envelope payload length |
| 48 | 32 | publisher key ID; equals envelope bytes 80–111 |

The header is followed by the game ID bytes, game version bytes, and entry
records. IDs and versions use the manifest's canonical ASCII representation and
are not NUL terminated.

Each entry record is `fixed || path`, where `fixed` is 56 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | path byte length |
| 2 | 2 | role |
| 4 | 4 | flags, zero in 2.0 |
| 8 | 8 | payload offset |
| 16 | 8 | file length |
| 24 | 32 | SHA-256 of file bytes |
| 56 | path length | canonical path bytes |

Entries MUST be sorted by raw path bytes, strictly increasing. The first
payload offset is zero. Every later offset MUST equal the previous offset plus
length. The final end MUST equal payload length. Zero-length files are allowed
only for role `data`; executable, manifest, jacket, and controls files cannot be
empty.

Roles:

| Value | Name | Cardinality/constraint |
| ---: | --- | --- |
| 1 | `manifest` | exactly one; path `game.toml` |
| 2 | `entry-x64` | exactly one in 2.0; PE32+ EFI application |
| 3 | `jacket-ocj1` | zero or one |
| 4 | `controls-v1` | zero or one |
| 5 | `data` | zero or more |

The manifest index MUST identify the sole role-1 entry. The manifest
`package_format`, `id`, `version`, `entry`, `jacket`, and `controls` values MUST
agree with the index and envelope.

## Canonical paths

Paths are printable ASCII using `/` separators. They MUST:

- be 1–240 bytes and relative;
- contain only `A-Z`, `a-z`, `0-9`, `.`, `_`, `-`, and `/`;
- have no empty, `.` or `..` component;
- not start or end with `/`;
- not contain `\\`, `:`, control bytes, or a drive prefix;
- not begin, after ASCII lowercase folding, with `efi/`, `games/`,
  `.openconsole/`, or `openconsole-`;
- be unique both byte-for-byte and after ASCII lowercase folding.

`game.toml` is the only permitted top-level manifest name. A bundle's paths are
relative to its own selected game slot and can never name parent or peer paths.

## Stable validation errors

Tools and firmware use the `OCG2_` prefix. Required codes include:

`BAD_MAGIC`, `UNSUPPORTED_VERSION`, `BAD_HEADER`, `LIMIT`, `OVERFLOW`,
`LENGTH_MISMATCH`, `INDEX_DIGEST`, `UNTRUSTED_KEY`, `BAD_SIGNATURE`,
`BAD_INDEX`, `BAD_PATH`, `PATH_COLLISION`, `BAD_ORDER`, `BAD_OFFSET`,
`BAD_ROLE`, `BAD_DIGEST`, `MANIFEST_MISMATCH`, and `BAD_EXECUTABLE`.

Errors identify an entry index and bounded byte offset where applicable. They
never include host filesystem paths, key material, or unbounded input strings.

## Bundle identity

The lowercase hexadecimal SHA-256 stored at header bytes 48–79 is the canonical
bundle ID. Re-signing the same canonical index with the same publisher identity
does not alter the bundle ID; changing any indexed path, role, offset, size,
digest, ID, version, or publisher key ID changes the index and therefore the ID.
Payload bytes are bound through their indexed digests.

Golden fixtures live in `tests/fixtures/ocg2/`. Each `.ocg` fixture has a
matching `.json` expectation containing its bundle ID, entries, and expected
result. Generated fixtures MUST be byte-for-byte reproducible.
