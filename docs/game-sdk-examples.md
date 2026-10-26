# Minimal game developer surface

OpenConsole does not require a binary SDK. Games use their language's UEFI
definitions plus the versioned protocol layouts in `include/openconsole`.

- `examples/sdk/c/package_root.c` demonstrates locating the running image's
  package directory from its own image handle and device path.
- `examples/sdk/c/minimal_game.c` demonstrates optional GUID protocol discovery
  and clean return.
- `examples/sdk/zig/abi_layout.zig` and `examples/sdk/rust/abi_layout.rs` are
  independently compiled layout guards for the published GLES, network, and
  audio structures.
- `tools/ocg2.py` migrates a legacy manifest and deterministically packs,
  inspects, or verifies a signed bundle.

Run `tests/sdk_examples_test.py` for the cross-language layout checks. These are
examples, not a promise that OpenConsole maintains language runtimes, package
managers, standard libraries, or compiler forks.
