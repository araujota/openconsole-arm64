# OpenConsole game manifest 2.0

`game.toml` remains a strict TOML-compatible, allocation-free profile rather
than general TOML. It is printable UTF-8 limited to ASCII in 2.0, at most
64 KiB, and uses one `key = value` per line. Strings, booleans, unsigned decimal
integers, bounded string arrays, blank lines, and end-of-line comments are the
only syntax. Unknown required keys, duplicate keys, tables, dotted keys,
multiline strings, floats, dates, Unicode escapes, and control bytes are errors.

## Required keys

```toml
format_version = "2.0"
package_format = "ocg2"
id = "orbit"
title = "Orbit"
version = "1.2.3"
entry = "bin/game.efi"
architecture = "x64"
required_capabilities = ["openconsole.gles2@0.1"]
optional_capabilities = ["openconsole.net@0.1", "openconsole.audio@0.2"]
graphics_api = "gles2"
accel = "optional"
min_graphics_abi = 1
```

Optional keys are `developer`, `description`, `website`, `jacket`, and
`controls`. `jacket` and `controls` name indexed files with the corresponding
role. `entry` names the sole `entry-x64` role. Their paths follow OCG2 canonical
path rules.

Limits retain the v1 field limits: ID 64, title/developer 128, version 32,
website/path 256, description 511, and eight entries in each capability array.
The logical `games/<id>` view and physical state directory component MUST
exactly equal `id`. The immutable bundle
ID is derived from the completed canonical OCG2 index and is supplied alongside
the parsed manifest; it is deliberately not embedded in the manifest because
that would create a self-referential digest.

`accel` is `"forbidden"`, `"optional"`, or `"required"`. `gop` requires
`forbidden`; the experimental `gles2` API accepts any value, but `required`
prevents launch unless an accelerated provider explicitly advertises the
negotiated ABI. This is capability negotiation, not a promise of GLES 2.0
conformance.

## Capability registry

Capability strings are lowercase ASCII `<name>@<major>.<minor>`. A provider is
compatible when majors match and its minor is greater than or equal to the
requested minor.

| Capability | Current version | Meaning |
| --- | --- | --- |
| `uefi.gop` | `1.0` | Standard firmware Graphics Output Protocol |
| `uefi.input.keyboard` | `1.0` | Standard console input |
| `uefi.input.pointer` | `1.0` | Standard Simple Pointer when present |
| `openconsole.gles2` | `0.1` | Experimental nonconformant GLES subset |
| `openconsole.net` | `0.1` | OpenConsole cooperative network protocol |
| `openconsole.audio` | `0.2` | OpenConsole cooperative PCM voice protocol |

Required capabilities block launch when absent or incompatible. Optional
capabilities never block launch and the game MUST tolerate their absence.
Unknown required names are unsupported; unknown optional names are ignored but
shown in technical details.

## Controls asset v1

The optional role `controls-v1` is printable ASCII, at most 16 KiB, one mapping
per line:

```text
left = "j"
right = "l"
sound = "k"
exit = "q"
```

Action names match `[a-z][a-z0-9_.-]{0,63}`. Values are one printable Unicode
scalar represented by an ASCII literal in v1 or one registered key name:
`enter`, `escape`, `space`, `left`, `right`, `up`, `down`, `mouse.left`, and
`mouse.right`. Duplicate actions and unknown syntax are rejected. The parent
may display mappings but does not translate game actions; the child reads its
own asset relative to its package root.

## Evolution

- Unknown major versions are rejected.
- A consumer may accept a newer minor only when every unfamiliar field begins
  with `x_` and its name appears in `ignorable_extensions`.
- Security, entry, identity, capability, and asset-role semantics can never be
  introduced as ignorable extensions.
- Publishers MUST NOT replace bytes for an already published bundle ID or reuse
  an `(id, version)` pair for a different bundle ID.

## v1 migration

The host migration tool reads the legacy eight-key profile, adds format and
version fields, translates protocol names to capability versions, converts the
boolean `accel` to the three-state policy, indexes conventional assets, and
prints every inferred value before signing. Firmware v1 discovery is available
only through an explicit development compatibility policy and labels the game
`UNVERIFIED LEGACY`; catalog publication accepts only v2.
