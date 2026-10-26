# Canonical development image

`make development-image PLATFORM_KEY=/secure/path/platform-ed448.pem` builds the
only standardized development FAT image. The key must be an Ed448 platform
inventory key; it is not a game-publisher or parent-update key.

The output directory is `build/development-image/` and contains:

- `openconsole-development.fat`, a 64 MiB FAT32 image with fixed volume metadata,
  sorted directory insertion, and normalized 2024-01-01 timestamps;
- the signed `EFI/OpenConsole/platform.ocp` inside that image;
- `platform-inventory.ed448.pub`, the detached verification input;
- `source-inventory.json`, `sbom-inputs.cdx.json`, and `profile.json` evidence;
- `SHA256SUMS`, which is transport evidence rather than runtime authority.

The development image includes the parent, baseline USB pointer driver, EDK II
network stack, integrity provider, optional audio driver, and empty canonical
state directories. It deliberately excludes legacy loose example games,
physical-probe logs/configuration, adjacent driver checksum files, private keys,
and test certificate fixtures. `platform.ocp` is the runtime authority for all
parent-loaded components.

The public key copied inside the FAT volume is an inspection/identity aid, not
a trust anchor: an attacker able to replace the image could replace both it and
the inventory. The development parent embeds the expected Ed448 public key and
CryptoDxe digest at build time. It verifies `platform.ocp`, the publisher-key
store, and each parent-loaded image immediately before `LoadImage`. Secure Boot
enrollment remains outside this development profile.

Build twice into separate directories and compare both payload inventory and
the complete FAT bytes:

```sh
./scripts/build-development-image.py --platform-key "$KEY" --output build/a
./scripts/build-development-image.py --platform-key "$KEY" --output build/b
./scripts/compare-development-images.py build/a build/b
```

The comparison exits nonzero and reports the first inventory path difference,
or the first differing image byte when payload inventories agree.
