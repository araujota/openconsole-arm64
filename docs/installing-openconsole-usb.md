# Installing OpenConsole on USB

After installation, open the USB's `OpenConsole/Host` folder from Windows or
Linux and double-click the clearly named launcher to prepare a one-time
OpenConsole restart through a native confirmation panel. Nothing runs
automatically on insertion, and the permanent boot order is not changed. See
[`host-reboot-launchers.md`](host-reboot-launchers.md) for requirements and the
firmware-settings fallback.

OpenConsole’s installers consume an extracted, architecture-specific payload and provision an entire removable disk. They do not write a fixed 64 MiB image onto a larger drive. Drives up to 8 GiB receive one FAT32 EFI System Partition. Larger drives receive a 1 GiB FAT32 ESP (`OC_BOOT`) and a second FAT32 volume (`OC_DATA`) that uses the remaining aligned capacity.

This keeps firmware boot files small and conventional while making the full device available for games. The parent locates `EFI/OpenConsole/data-volume.json`, then routes games, state, recovery records, logs, and deletion to that uniquely identified data volume. Drivers, trust roots, and updates remain on the ESP. OCG2 bundles and the game authoring interface are unchanged.

## Before erasing anything

Back up the target. Installation destroys its partition table and files. First run the installer without its erase flag, verify model, serial, capacity, transport, and path/disk number, then copy the displayed identity token into the destructive invocation. Both installers re-read identity immediately before mutation and refuse active system disks, mounted volumes, and unsuitable device types.

Physical USB installation has not yet been exercised in this development release. Sparse loop-device tests on Linux and static/CI checks on Windows do not constitute physical-device evidence.

## Linux

List candidates and preview a target:

```sh
sudo ./scripts/install-openconsole-linux.py --list
sudo ./scripts/install-openconsole-linux.py \
  --device /dev/sdX --payload ./openconsole-x64 --arch x64
```

After checking the preview, repeat with the exact token:

```sh
sudo ./scripts/install-openconsole-linux.py \
  --device /dev/sdX --payload ./openconsole-x64 --arch x64 \
  --erase --confirm TOKEN --report openconsole-install-report.json
```

Use `--arch arm64` with a payload containing `EFI/BOOT/BOOTAA64.EFI`. The installer creates GPT partitions, formats FAT32, copies and byte-verifies every payload file, flushes storage, re-reads the marker, unmounts, and emits a versioned JSON report.

## Windows

Open an elevated PowerShell. List disks with `Get-Disk`, then preview:

```powershell
.\scripts\install-openconsole-windows.ps1 `
  -DiskNumber 4 -Payload .\openconsole-x64 -Architecture x64
```

After checking the preview, use its exact token:

```powershell
.\scripts\install-openconsole-windows.ps1 `
  -DiskNumber 4 -Payload .\openconsole-x64 -Architecture x64 `
  -Erase -Confirm TOKEN -Report .\openconsole-install-report.json
```

Large drives require the repository-owned `oc-fat32-format.exe`, built from `tools/oc-fat32-format.c`; CI publishes the pinned Windows binary. Pass `-Fat32Formatter` if it is stored elsewhere. Windows’ built-in formatter is used only for the 1 GiB ESP.

## Limits, upgrades, and recovery

FAT32 limits each individual file to 4,294,967,295 bytes. OpenConsole’s current authenticated game limit is smaller (64 MiB), and the parent checks both free space and per-file limits before staging. Keep a copy of downloaded OCG2 bundles and the installer report before reprovisioning. Reinstalling erases state; copying a payload update onto the ESP is a separate, authenticated update workflow.

If firmware does not show the drive, verify GPT, the `OC_BOOT` FAT32 ESP, and the architecture-specific default filename. If games do not appear, ensure exactly one attached filesystem contains a valid data-volume marker. Multiple matching markers deliberately disable mutations. Use `tools/verify_media_layout.py` and the JSON install report when diagnosing capacity or alignment issues.
