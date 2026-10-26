# OpenConsole network and audio evidence matrix

Date: 2026-10-24

This matrix keeps VM acceptance separate from future physical-hardware
qualification. A VM pass validates the public protocols, parent/child ownership,
packaged driver closure, and deterministic failure handling for this increment.
It does not claim support for a not-yet-selected development rig or prove sound
at a physical connector.

| Area | Environment | Result | Evidence | Release implication |
| --- | --- | --- | --- | --- |
| Host mocks and failure recovery | ASan/UBSan host tests | Pass | `build/evidence-host-tests.log` | Protocol state machines, bounds, crypto, mixer, and cleanup are regression-covered. |
| Freestanding EFI and manifests | PE/COFF link and manifest checks | Pass | `build/evidence-freestanding.log` | Core EFI objects link without hosted dependencies; integrated child declares GLES2 ABI 1. |
| Offline parent/child | OVMF, no network/audio dependency | Pass | `build/evidence-offline.log`, `build/openconsole-offline.png` | Network/audio absence does not break the launcher. |
| Packaged network | OVMF + virtio-net + isolated fixtures | Pass | `build/evidence-network.log`, `build/network-fixture-metrics.json`, `build/ovmf-test-network-fixture.log` | DHCP, DNS, UDP, HTTP, bounds, timeout, repeat launch, and zero provider allocations after cleanup are proven in VM. |
| Packaged audio | OVMF + Intel HDA + QEMU WAV backend | Pass | `build/evidence-audio.log`, `build/ovmf-audio-metrics.json`, `build/ovmf-audio.wav`, `build/ovmf-test-audio.log` | AudioDxe discovery, PCM playback, stereo placement, overlap, silence, recovery, and child DMA cleanup are proven in VM. |
| Authenticated update | OVMF + deterministic HTTPS fixture | Pass | `build/evidence-update.log`, `build/ovmf-test-update-e2e.log` | Valid update and rejection-with-preservation paths are proven in VM. |
| Simultaneous child workload | OVMF GLES2 + input + virtio-net + HDA | Pass | `build/evidence-integrated.log`, `build/ovmf-integrated-io-metrics.json`, `build/ovmf-integrated-audio-metrics.json`, `build/openconsole-integrated.png` | Two launches rendered progressing GLES frames, handled input, completed DHCP/UDP, played mixed audio with zero underruns, and returned cleanly. |
| Firmware protocol discovery | Future development rig | Deferred to hardware-specific qualification | `OPENCONSOLE-INVENTORY.LOG` can be collected using `docs/physical-io-bringup.md`; `scripts/verify-physical-test-media.py` emits preflight/collected integrity receipts | The reusable workflow is ready; no physical device is claimed by this VM-scoped change. |
| Physical-media inventory persistence | Exact physical-test EFI/ESP under OVMF | Pass | `make test-physical-inventory-ovmf`; `build/ovmf-physical-inventory-metrics.json`; `build/ovmf-physical-inventory-media-receipt.json` | Proves both inventory stages persist and pass collection integrity checks; explicitly not physical-hardware evidence. |
| Audio-driver selection | Two-stage OVMF HDA inventory | Pass | Firmware-stage `OPENCONSOLE-INVENTORY.LOG` emits `OPENCONSOLE_IO_AUDIO_DRIVER_DOWNLOAD_OPENCORE_AUDIODXE`; post-load `OPENCONSOLE-DRIVER-INVENTORY.LOG` emits `OPENCONSOLE_IO_AUDIO_DRIVER_USE_EXISTING_AUDIOIO` | A first boot can distinguish existing AudioIo, HDA requiring AudioDxe, USB requiring a separate UAC backend, and unsupported audio hardware. |
| Exact NIC backend | Future development rig Ethernet | Deferred; probe/analyzer ready | Token-bound `Physical Network Probe`, second-host observer, and `scripts/analyze-physical-network-evidence.py` correlate complete runs, validated record hashes/timestamps/peers, paired UDP+HTTPS, and exact-device firmware inventory | Select firmware SNP/UNDI or review a packaged driver only after the rig's exact NIC is known. |
| Analog HDA output | Future development rig analog connector | Deferred; analyzer ready | `scripts/analyze-physical-audio-evidence.py` binds an external WAV by SHA-256 and checks silence, 440-Hz music, left-panned overlap, stereo balance, gain, clipping, and recovery | Qualify the exact controller, codec, and connector in the later hardware change. |
| HDMI and USB audio | Future development rig | Deferred, separate backends | Inventory tooling distinguishes controller, codec, and USB class identities | Neither backend may be used as evidence for analog HDA unless explicitly selected and tested. |
| AudioDxe controller-without-codec recovery | OVMF Intel HDA, no codec | Passing | `make test-audio-no-output-ovmf`; `build/ovmf-test-audio-no-output.log`; `build/ovmf-audio-no-output-metrics.json`; `build/openconsole-audio-no-output.png` | Proves scoped disconnect/unload and offline parent survival, not physical audio. |

## Integrated budgets observed

The final integrated run recorded two launches with 194 and 188 GLES frames,
DHCP completion at 3.60 and 3.66 seconds, UDP completion at 40 milliseconds,
two input-triggered effects, zero audio underruns, and 12,288 queued audio
frames. The two time-separated screenshots had different hashes, proving visual
progress rather than merely finding a static frame marker.

The 12,288-frame audio budget is the configured three-buffer AudioIo pipeline:
three 4,096-frame buffers, or 256 ms at 48 kHz. It is a queue-depth bound, not a
claim of measured physical output latency.

## Reproduction commands

```sh
make test-host
make check-efi-core check-examples
make test-offline-ovmf
make test-packaged-network-fixture-ovmf
make test-audio-ovmf
make test-update-e2e-ovmf
make test-integrated-io-ovmf
```

Each VM scene now writes a distinct `build/ovmf-test-<scene>.log` and screenshot.
Generated `build/` artifacts are reproducible VM acceptance evidence. They are
not substitutes for physical evidence in a later hardware-qualification change.

## Readiness decision

The host and OVMF acceptance gates are green, so this VM-scoped network/audio
increment is **ready**. Physical support remains unclaimed and must be qualified
separately after the dedicated development rig and its exact devices are chosen.
