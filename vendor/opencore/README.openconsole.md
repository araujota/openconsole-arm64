# OpenConsole OpenCore AudioDxe source boundary

- AudioDxe upstream: `https://github.com/acidanthera/OpenCorePkg.git`
- AudioDxe pin: `78d5ff824992d5b227f842e34d8221eb8f31baf1`
- Compatible build base: `https://github.com/acidanthera/audk.git`
- audk pin: `dfd5516702b4911f1fc1919a429d637b456df913`
- Selected upstream API/source: `Include/Acidanthera/Protocol/AudioIo.h`
  and `Staging/AudioDxe/`.
- License: the selected AudioDxe and AudioIo files carry the MIT grant. The
  OpenCore repository aggregate is BSD-3-Clause; audk is a
  BSD-2-Clause-Patent EDK II fork. File-level notices remain authoritative.
- Local patches: `patches/opencore-audiodxe/` contains the reviewed failure
  cleanup and instance-isolation changes applied by the build. Their exact
  digests are recorded in `dependency-provenance.json`. OpenConsole adapters
  belong under `src/audio/`, never mixed into fetched upstream trees.

`scripts/build-opencore-audiodxe.sh` fetches both immutable revisions, uses the
submodule revisions recorded by audk, builds only `AudioDxe.inf`, and emits
`AudioDxe.efi`, commit markers, and `SHA256SUMS` under
`build/opencore-audiodxe/`. OpenCore's DSC validates paths for unrelated
components, so the reproducible source checkout must be complete even though
the packaged runtime payload is only AudioDxe.

AudioDxe is a UEFI driver for PCI class 04/03/00 HDA controllers. It discovers
codecs and publishes revision 4 of the OpenCore `AudioIo` protocol. The initial
VM target is QEMU `ich9-intel-hda` with `hda-output`; the physical AMD
`1022:15e3` analog path remains an explicit hardware-evidence requirement and
is not claimed compatible merely because the controller class matches.

The reviewed source surface is deliberately referenced by immutable pin rather
than copied and modified. This avoids a misleading partial source snapshot:
AudioDxe links several OpenCore libraries and must be rebuilt against its
matching audk/OpenCore dependency closure. Release artifacts must retain both
commit marker files and the hash manifest.
