# Physical Ethernet and audio bring-up

## Named-host firmware preflight

The target is an MSI MAG X670E TOMAHAWK WIFI (`MS-7E12`). Linux currently
identifies the wired controller as RTL8125 `10ec:8125` revision `05` and the
analog HDA controller as AMD `1022:15e3` with a Realtek ALC1220 codec. Those
Linux observations identify the hardware but do **not** satisfy any UEFI task.

Before the first USB inventory, enter MSI Click BIOS with Delete, press F7 for
Advanced mode, and inspect `Settings -> Advanced -> Integrated Peripherals`:

- Keep `Onboard LAN Controller` enabled.
- Enable `Network Stack` and `IPv4 PXE Support` for the inventory run. `LAN
  Option ROM` is the legacy/CSM setting and is not the UEFI control we need.
- Keep `HD Audio Controller` enabled.
- Disable Secure Boot for the current unsigned development image, without
  changing CSM, storage, overclocking, or unrelated device settings.

MSI's official AM5 BIOS manual documents `Network Stack` as the UEFI network
stack switch, makes IPv4 PXE conditional on it, and lists the HDA controller
switch on the same Integrated Peripherals page:
<https://download.msi.com/archive/mnu_exe/mb/AMDAM5BIOS.pdf>. MSI also documents
the Advanced-mode menu path here:
<https://www.msi.com/faq/8734>. Record the observed values with the returned
inventory. Enabling these settings is a test precondition, not proof that the
firmware publishes SNP/UNDI for the exact RTL8125 controller; only the
controller-associated USB inventory can decide task 7.2 versus 7.3.

`make physical-test-package` creates `build/esp` and a transferable
`build/openconsole-physical-test-esp.zip` with the fallback
`EFI/BOOT/BOOTX64.EFI`, the pinned EDK II network stack, the reviewed AudioDxe,
the crypto provider, child games, and a preallocated 16-KiB
`OPENCONSOLE-INVENTORY.LOG`. Copy the *contents* of that directory to a FAT32
USB EFI System Partition. This target never chooses or writes a block device.
The archive contains `OPENCONSOLE-PAYLOAD-SHA256SUMS`; from the copied FAT32
partition, run `sha256sum -c OPENCONSOLE-PAYLOAD-SHA256SUMS` before booting.
The four writable evidence logs are intentionally excluded from that manifest.
Prefer the stricter media verifier, which also rejects unmanifested payloads,
symlinks, unsafe manifest paths, missing probes, and incorrectly sized logs:

```sh
./scripts/verify-physical-test-media.py /path/to/mounted-usb \
  --output build/physical-media-preflight.json
```

Alternatively, on a Linux build host with loop-device access, create a complete
64-MiB GPT/FAT32 removable-media image:

```sh
make physical-test-image
```

This produces `build/openconsole-physical-test.img`. The builder accepts no
device argument, writes only that regular file, mounts its private loop
partition, and validates the copied FAT contents with the same strict verifier
after remounting them read-only. It also writes
`build/physical-image-preflight.json`. Flash the image only after independently
identifying the intended removable drive; never select either internal NVMe.
The image builder itself deliberately does not flash any device or alter UEFI
boot entries.

After the firmware boot, run it again with `--collected`. This requires both
inventory stages to contain their completion marker and emits a receipt binding
every returned log and the immutable payload manifest by SHA-256:

```sh
./scripts/verify-physical-test-media.py /path/to/mounted-usb --collected \
  --output build/physical-media-collected.json
```

The physical-only boot image records two inventories: the firmware-only state in
`OPENCONSOLE-INVENTORY.LOG`, then the state after OpenConsole attempts to load
its packaged network stack and AudioDxe in
`OPENCONSOLE-DRIVER-INVENTORY.LOG`. Never use the second file as evidence that
the firmware itself supplied a protocol.

`make test-physical-inventory-ovmf` boots this exact physical-test EFI and ESP,
then proves both fixed-size inventory files are opened, flushed, persisted, and
accepted by the collected-media verifier. Its metrics deliberately set
`physical_hardware_evidence` to `false`: this is a handoff/persistence test and
cannot replace the named-host USB boot.

The `Physical Audio Probe` game submits the reference 48-kHz stereo music and
mono effect mix through the public child ABI. Select it after inventory capture
to test the named physical output; its return also exercises parent-owned voice
and DMA teardown. Record the connector used and an independent capture or
listening observation—protocol/log success alone is not physical-audio proof.
OpenConsole now enumerates every compatible AudioIo port rather than binding the
first global protocol. Public output names have the form
`HDA VVVV:DDDD rev RR <device> port PP loc LL surf SS` or
`USB Audio VVVV:DDDD rev RR <device> port PP loc LL surf SS`; the PCI identity comes
from the nearest device-path ancestor and `<device>` distinguishes `line`,
`speaker`, `headphones`, `spdif`, and `hdmi`. Output selection stops outstanding DMA and
re-runs AudioIo setup for that exact protocol instance and port mask. For task
7.4, select an output named `HDA 1022:15E3 ...` whose device/connector matches
the analog jack under test. An `hdmi` entry—including one on that controller—or
an output belonging to another PCI function must be recorded separately and
does not count as analog evidence.

On the first physical boot, launch `Physical Audio Probe` once without an
`OPENCONSOLE-AUDIO.CFG`. Retrieve `OPENCONSOLE-AUDIO-EVIDENCE.LOG` and choose
the exact analog output from its `OUTPUT` records. Copy
`OPENCONSOLE-AUDIO.CFG.example` to `OPENCONSOLE-AUDIO.CFG`, retaining the exact
reported `output_index`, PCI identity, and analog device type (`line`, `speaker`,
or `headphones`), then boot and launch the probe twice. The named host's
`1022:15e3` codec reports as Realtek `10ec:0b00` / ALCS1200A in Linux (despite
the SMBIOS ALC1220 label): rear line-out pins `0x14`-`0x16`, front headphones
at `0x1b`, and optical S/PDIF at `0x1e`. For the rear analog jack, expect an
enumerated `HDA 1022:15E3 ... line ... loc 01 surf 00` output; use the exact
index actually recorded by UEFI. Capture or independently listen to that named
jack.
The analyzer deliberately rejects implicit/default selection, a selected HDMI
or S/PDIF output, fewer than two complete lifecycles, and inventories that do not keep HDMI
and USB Audio separate:

```sh
./scripts/analyze-physical-audio-evidence.py \
  OPENCONSOLE-AUDIO-EVIDENCE.LOG \
  --connector "motherboard rear line-out" \
  --observation "captured reference mix through motherboard rear line-out" \
  --capture-wav external-line-output.wav \
  --inventory-report build/physical-inventory-evidence.json \
  --output build/physical-audio-evidence.json
```

For objective capture evidence, start a 48-kHz signed 16-bit stereo WAV
recording before launching the probe and keep recording through its final
recovery tone. The analyzer binds that recording by SHA-256 and verifies leading
and middle silence, centered approximately 440-Hz music, left-panned effect
overlap, stereo balance, non-clipping gain, and recovery. Omitting
`--capture-wav` is allowed for an independent listening witness, but the report
labels that evidence `subjective-listening` rather than `objective-capture`.

USB Audio discovery and playback are intentionally separate facts. The named
host currently exposes the DisplayLink D6000 USB Audio interface `17e9:6006` to
Linux. The UEFI inventory now identifies USB Audio-class interfaces through
`EFI_USB_IO_PROTOCOL`, but the pinned EDK II/OpenCore package contains no USB
Audio streaming driver. Consequently the physical report may show
`usb_audio_17e9_6006_discovered: true` and `usb_audio_playable: false`; it must
not relabel the device as HDA or imply that HDA/HDMI playback exercised it.

## Physical Ethernet endpoint

After `make physical-test-esp`, choose a second computer on the same wired LAN
and note its IPv4 address. Prepare an endpoint-specific short-lived CA, USB
configuration, and second-host fixture bundle (replace the example address):

```sh
./scripts/prepare-physical-network-test.py 192.168.1.50
./scripts/package-physical-test-esp.sh
```

Copy `build/physical-network-fixture`, plus
`scripts/run-physical-network-fixture.py`, to the second host. Permit inbound
UDP/9000 and TCP/9443 only from the test LAN, then start the observer there:

```sh
./run-physical-network-fixture.py physical-network-fixture
```

Copy the repackaged ESP to USB, boot it, and launch `Physical Network Probe`
twice. The child obtains its own DHCP lease, resolves the configured DNS name,
exchanges a tokenized UDP datagram, validates the endpoint-specific TLS
certificate, and appends results to
`OPENCONSOLE-NETWORK-EVIDENCE.LOG`. The second host independently appends peer,
timestamp, payload, and SHA-256 records to `observed-requests.jsonl`.

After returning both files, correlate two complete child lifecycles with two
externally observed UDP and HTTPS requests:

```sh
./scripts/analyze-physical-network-evidence.py \
  OPENCONSOLE-NETWORK-EVIDENCE.LOG \
  observed-requests.jsonl physical-network-fixture/fixture.json \
  --inventory-report build/physical-inventory-evidence.json \
  --output build/physical-network-evidence.json
```

The analyzer accepts only complete child runs containing the current randomized
fixture token. It validates every observer record's schema, SHA-256, peer IP,
timezone-aware timestamp, and fixture validity window; requires token-bound UDP
and HTTPS requests from the same peer within 120 seconds; rejects duplicate or
replayed records; and independently requires the firmware inventory to associate
SNP/UNDI with exact RTL8125 `10ec:8125` revision 05. The resulting report hashes
the USB log, observer log, fixture, and inventory report.

This evidence can satisfy task 7.2 only if the firmware inventory associates
SNP/UNDI with exact RTL8125 `10ec:8125` revision 05. OVMF, another NIC, or the
second-host loopback self-test cannot substitute for that association.

Disable Secure Boot for this unsigned development image, boot the USB entry,
wait for the OpenConsole home screen, and shut down normally. Copy
`OPENCONSOLE-INVENTORY.LOG` back to the workspace and validate it with:

```sh
./scripts/analyze-physical-inventory.py OPENCONSOLE-INVENTORY.LOG \
  --post-driver-log OPENCONSOLE-DRIVER-INVENTORY.LOG \
  --output build/physical-inventory-evidence.json
```

Also retrieve `OPENCONSOLE-DRIVER-INVENTORY.LOG`. The analyzer compares its
controller/codec/AudioIo descendants with the firmware-only state and emits the
RTL8125 branch decision. The comparison does not itself prove audible physical
output.

The log is captured before OpenConsole loads any packaged network or audio
driver. Per-controller `PCI_PROTOCOLS` records associate firmware SNP/UNDI and
AudioIo/HDA handles by device-path ancestry. This distinguishes firmware support
for RTL8125 `10ec:8125` revision 05 and each named HDA controller from unrelated
global protocol handles. A zero RTL8125 SNP/UNDI result selects task 7.3; a
nonzero result selects task 7.2. HDMI devices remain separate from the AMD
`1022:15e3` analog path and cannot satisfy its physical-output evidence.

If task 7.3 is selected, do not add `10ec:8125` to an RTL8168/8169 allowlist.
Realtek now lists an official x64-capable UEFI UNDI package, version `2.081`
(download ID `3413`), for the RTL8125 family. Its CAPTCHA and unpublished
redistribution terms mean it is a manually acquired, review-required candidate,
not part of the generated ESP. Preserve the original archive and do not copy its
driver into `EFI/OpenConsole/Drivers/Network` until the dedicated exact-device
change records its terms, SHA-256, PE architecture/type, device allowlist, load
order, SNP publication, teardown, and physical packet evidence.

The existing Network directory is an upper-stack directory, not a hardware
driver drop-in point. Its loader begins only after an SNP controller exists.
Task 7.3 must introduce and test an explicit pre-SNP bootstrap; copying the UNDI
binary into the current package will not make an SNP-less RTL8125 usable.

Do not check task 7.1 from Linux `lspci`, OVMF, or a successful packaged-driver
load. It requires the returned log from a real firmware boot on the named host.
