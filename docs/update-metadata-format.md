# OpenConsole catalog and update metadata v1

Both formats are bounded little-endian binary records. Decoders reject unknown
versions, nonzero reserved fields, trailing bytes, control characters, empty
required strings, and lengths outside the limits in `update_metadata.h`. This
makes every accepted logical record have exactly one wire encoding.

## Catalog (`OCC1`)

The 32-byte header contains magic, version 1, header size, total size, a zero
reserved field, monotonically increasing catalog sequence, and Unix expiration.
It is followed by a 16-bit entry count and at most 32 entries. Each entry has
bounded ID, display-name, and metadata-URL lengths; artifact version and byte
size; and a SHA-256 digest. The complete catalog is capped at 64 KiB.

The catalog is discovery information. It never authorizes executable bytes;
each selected entry points to independently signed `OCU1` metadata.

## Signed update (`OCU1`)

The fixed 287-byte header contains:

- version, exact total length, flags, and reserved field;
- monotonically increasing metadata sequence and Unix expiration;
- artifact version and exact byte size;
- 16-byte signing key ID;
- optional next-key ID and 57-byte Ed448 public key;
- SHA-256 artifact digest;
- 114-byte Ed448 signature;
- bounded artifact URL length, followed by at most 512 URL bytes.

The signature input is not the in-memory structure. It is the canonical byte
string emitted by `oc_update_write_signed`: ASCII `OCU1-SIGNED`, format version,
flags, sequence, expiration, artifact version/size, current and next key fields,
artifact digest, URL length, and URL. The signature field is deliberately
excluded from its own domain. Maximum metadata size is 4 KiB.

## Rollback and key rotation

Policy acceptance happens only after task 4.2 verifies the signature against
the key identified by `signing_key_id`. The policy rejects expired metadata,
sequences no greater than the highest accepted sequence, artifact versions no
greater than the installed version, artifacts above the configured byte limit,
and unknown key IDs.

A rotation record must be signed by the current trusted key and carries a
distinct nonzero next-key ID/public key. That key becomes pending. A subsequent
valid record signed by the pending key promotes it to current; a pending key may
not itself authorize another rotation. Persisting the accepted sequence and key
state atomically belongs to the staged-update work in task 4.3.
