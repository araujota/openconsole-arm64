# OpenCore AudioDxe failure-path audit

Scope: OpenCorePkg commit `78d5ff824992d5b227f842e34d8221eb8f31baf1`,
built through audk commit `dfd5516702b4911f1fc1919a429d637b456df913`.
This is source and OVMF evidence, not physical audio evidence.

The reviewed local patches are
`patches/opencore-audiodxe/0001-failure-cleanup.patch` and
`patches/opencore-audiodxe/0002-instance-safe-playback-setup.patch`. They fix
the following behavior found during the audit:

- Short PCI common-buffer mappings for a BDL, stream data, or DMA-position
  table now fail with `EFI_NO_MAPPING` instead of accidentally returning
  success.
- Controller and codec private protocol objects become cleanup-visible only
  after transactional protocol installation succeeds. Failed codec publication
  can no longer leave dangling pointers that outer cleanup frees twice.
- Stream stop attempts to halt DMA and clears callbacks/source state even when
  cancelling the polling timer reports an error. Final controller cleanup also
  closes the event.
- Upstream's playback-setup cache was process-global rather than per AudioIo
  instance. On a system with NVIDIA HDMI, AMD HDMI, and AMD analog codecs, a
  second codec requesting the same port mask and format could return success
  without configuring that codec. The local patch removes the unsafe shortcut;
  every `SetupPlayback` resolves and programs the selected protocol instance.

All controller-start failures converge on `HdaControllerCleanup`, which closes
per-stream timers, stops streams, unmaps and frees BDL/data/position common
buffers, stops CORB/RIRB, resets the controller, uninstalls published protocols,
and restores saved PCI attributes. Codec-start failures converge on
`HdaCodecCleanup`, which removes AudioIo and codec protocols and frees parsed
codec allocations. The executable audit gate verifies these invariants against
the exact patched source and records the packaged binary size and source commit.

The launcher also handles the post-start/no-output case. After attempting to
connect AudioDxe to HDA-class PCI handles, a missing AudioIo protocol triggers
`DisconnectController` for only the AudioDxe image on each attempted HDA
controller. This invokes the reviewed Driver Binding `Stop` paths before the
launcher calls `UnloadImage`; unrelated controller drivers are not selected.
The launcher retains the image handle if firmware refuses the unload instead of
forgetting a potentially live driver. This path emits
`OPENCONSOLE_AUDIO_DRIVER_NO_OUTPUT_CLEANUP` and leaves the public OpenConsole
audio protocol uninstalled.

`make test-audio-no-output-ovmf` dynamically covers this path with QEMU
`ich9-intel-hda` and deliberately no codec device. The test requires exactly one
driver start and cleanup marker, requires `OPENCONSOLE_AUDIO_OFFLINE_NO_AUDIOIO`,
rejects any `OPENCONSOLE_AUDIO_PROVIDER_READY`, and captures the still-rendered
parent menu. Evidence is retained in
`build/ovmf-test-audio-no-output.log`,
`build/ovmf-audio-no-output-metrics.json`, and
`build/openconsole-audio-no-output.png`. The JSON binds the exact log and PNG
with SHA-256 digests and records the singular start/cleanup counts, absence of
provider publication, offline fallback, and clean QEMU exit. This proves VM failure recovery only;
it does not prove physical HDA output or teardown.

Run it with `make audit-opencore-audiodxe`.
