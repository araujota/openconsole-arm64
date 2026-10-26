<!-- SPDX-License-Identifier: Apache-2.0 -->
# Runtime-integrity OVMF matrix

This matrix is development-VM evidence. It does not claim process isolation,
guaranteed watchdog preemption, Secure Boot enrollment, or physical-firmware
coverage.

| Boundary | OVMF exercise | Required evidence |
| --- | --- | --- |
| Normal return | First integrated-game launch returns success | child return, restored input/display, zero live network/audio generation resources |
| Error return | Second integrated-game launch returns an EFI error with live resources | error is reported and parent cancels/frees graphics, network, audio, pointer, and generation state |
| Mutation after discovery | Instrumented parent overwrites the retained signed bundle after selector recovery but before authenticated reopen | reopen rejects; `LoadImage` is never reached |
| Corrupt driver | A byte is changed in inventoried `AudioDxe.efi` on a real FAT image | digest authorization rejects it, driver never starts, baseline dashboard remains usable |
| Unsupported watchdog | Instrumented firmware call returns `EFI_UNSUPPORTED` | stable diagnostic emitted; attempt/completion journals and child return still succeed |
| Hang/reset | Signed child enters a non-returning loop after the attempt record is flushed; VM is stopped and the same FAT image is cold-booted | exact bundle is quarantined on the next process/boot boundary |
| Safe mode | Cold boot observes the incomplete attempt | optional audio driver is suppressed while selector and quarantined game details remain visible |
| Manual retry | Recovery fixture explicitly presses `R`, then launches and returns | quarantine is cleared only by user action; new attempt and completion records persist |

The mutation and unsupported-watchdog builds are explicitly named test parents;
neither test hook is compiled into `BOOTX64-DEVELOPMENT.EFI`. The hang/reset
case uses a separate QEMU process for the second boot so persistence is proven
across an actual block-device close/reopen boundary rather than an in-memory
firmware reset.

Run the complete local evidence with:

```sh
make test-integrated-io-ovmf
make test-runtime-integrity-ovmf
```

The latter target also creates two byte-identical canonical development images
and runs the existing incomplete-attempt quarantine/manual-retry scene before
the four dedicated integrity cases.
