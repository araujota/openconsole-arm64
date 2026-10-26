# OpenConsole ARM64 / AArch64

> We have to go where they can’t follow.

This is the separately released ARM64 suitcase for OpenConsole: a native UEFI
game launcher intended to live on a USB drive beside signed OCG games. ARM64
firmware starts `EFI/BOOT/BOOTAA64.EFI`; ARM64 games declare
`architecture = "arm64"` and use the signed OCG2 `entry-arm64` role.

This repository is generated from the same reviewed portable source and public
ABI definitions as [the x64 runtime](https://github.com/araujota/openconsole).
Architecture-neutral fixes are synchronized by deterministic export and shared
contract hash checks rather than maintained as a hand-edited second engine.

## Current evidence

The first release is an **AAVMF-validated development preview**. Automated QEMU
`virt` evidence covers native parent boot, dashboard rendering, ARM64 game
discovery, parent-to-child launch, injected keyboard input, child return, and
framebuffer capture. It does not establish physical ARM64 hardware, networking,
audio, Secure Boot, or GPU-acceleration compatibility.

## Build and test

On Ubuntu/Debian, install Clang/LLD, `qemu-system-arm`, `qemu-efi-aarch64`,
`dosfstools`, `mtools`, Python 3, and the Python requirements.

```sh
make test-arm64-host
make arm64-image
make test-arm64-aavmf
```

The deterministic image is
`build/arm64/openconsole-arm64-development.fat`. Its default boot application is
always checked as PE32+ machine `0xAA64`, subsystem 10. Every EFI in the image is
inventoried before release.

- [Developer documentation](https://openconsole-araujota97gmailcoms-projects.vercel.app/docs)
- [x64 runtime](https://github.com/araujota/openconsole)
- [Catalog source](https://github.com/araujota/openconsole-catalog)
- [ARM64 maintenance policy](docs/arm64-maintenance.md)

EFI children are trusted native firmware code. Signatures and analysis are not
a sandbox. Apache-2.0; see `CONTRIBUTING.md`, `SECURITY.md`, and `NOTICE`.
