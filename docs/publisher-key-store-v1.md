# OpenConsole publisher key store (`OCPK` 1.0)

`EFI/OpenConsole/Trust/PublisherKeys.ocpk` is the canonical game-publisher trust
store. Its exact length and SHA-256 are authorized by `platform.ocp`; it is not
self-signed and must never be trusted merely because it is adjacent to a game.

The 64-byte little-endian header is: magic `OCPK`, major/minor `1/0`, header
size `64`, record count (maximum 256), record-region byte length (maximum 64
KiB), flags (zero), total file length, and SHA-256 of the record region.

Each sorted record is `104-byte prefix || publisher ID || display name`:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 32 | key ID: SHA-256 of raw public key |
| 32 | 57 | raw Ed448 public key |
| 89 | 4 | scopes; bit 0 permits OCG2 game signing |
| 93 | 1 | state: `1` active, `2` revoked |
| 94 | 1 | publisher-ID length, 1–64 |
| 95 | 1 | display-name length, 1–64 |
| 96 | 8 | reserved zero bytes |
| 104 | variable | lowercase publisher ID, then printable-ASCII display name |

Records are strictly sorted by raw key ID and key IDs are unique. Publisher IDs
use `[a-z0-9][a-z0-9_-]*`; multiple rotated keys may share one publisher ID.
A revoked record remains present so an installed bundle cannot become
ambiguously “unknown.” New scopes, states, flags, or major versions fail closed.
Trust-store replacement is a signed platform update that also replaces the
authorizing `platform.ocp`; catalog metadata and OCG2 bundles cannot add keys.
