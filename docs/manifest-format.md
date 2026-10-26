# Game manifest profile (legacy v1)

This profile is retained only for migration fixtures. New packages use
`game-manifest-v2.md` inside authenticated OCG2 bundles.

OpenConsole intentionally accepts a strict TOML-compatible profile for the
security-sensitive boot-time manifest. This is not presented as a general TOML
parser. Files are UTF-8 byte sequences no larger than 64 KiB.

```toml
id = "hello"
title = "Hello, OpenConsole"
entry = "game.efi"
architecture = "x64"
required_protocols = []
graphics_api = "gop"
accel = false
min_graphics_abi = 0
```

All eight keys are required exactly once; unknown and duplicate keys are errors.
The current profile supports basic quoted strings, booleans, unsigned decimal
integers, arrays of at most eight basic strings, blank lines, and end-of-line
comments. Tables, dotted keys, dates, floats, multiline/literal strings, Unicode
escapes, and inline tables are rejected.

`id` is 1–64 bytes and matches `[a-z0-9][a-z0-9_-]*`. `entry` is a forward-slash
relative path of at most 256 bytes. Absolute paths, drive prefixes, backslashes,
empty components, `.` and `..` components, and control bytes are rejected.
`architecture` must currently be `x64`. `graphics_api` is `gop` or `gles2`, and
`accel=true` is incompatible with `gop`.

This profile may gain optional namespaced keys only through a versioned format
decision. Limits are part of the parsing contract and apply before filesystem
access or EFI image loading.
