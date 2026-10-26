# Host reboot launchers

Every x64 and ARM64 OpenConsole USB payload includes ordinary host files at
`OpenConsole/Host`. They are not EFI drivers and are never loaded by the parent
or by a game. On Windows, double-click **Open OpenConsole on next reboot.cmd**.
On Linux, run or double-click **Open OpenConsole on next reboot.sh**.

USB insertion deliberately does not run anything. Current Windows and Linux
security models do not provide a safe portable-USB autorun mechanism. A resident
watcher would require installation and expand the trusted surface, so it is not
part of the baseline.

## What happens

The launcher validates its containing removable device, canonical installation
marker, signed-platform inventory location, architecture-specific fallback EFI
file, partition GUID, and whole USB identity. It then searches existing firmware
entries for the exact partition and path. A friendly firmware description is
never sufficient. Duplicate or ambiguous matches fail closed.

If there is exactly one match, the native panel can prepare that entry for the
next restart only. Linux uses `efibootmgr --bootnext`; Windows sets the firmware
manager's `bootsequence`. Both re-discover the USB after elevation and verify a
readback. They do not alter persistent boot order. A second panel asks whether
to restart, so choosing “later” preserves open work.

If direct selection cannot be proven, the launcher offers a separately labeled
firmware-settings fallback (`systemctl reboot --firmware-setup` or
`shutdown /r /fw`). The user must choose the USB in firmware. This fallback is
not presented as successful direct selection.

Linux prefers Zenity, then KDialog, and otherwise supplies an explicit terminal
confirmation. It requires Python 3, `lsblk`, `efibootmgr`, `systemctl`, and
`pkexec`; these are host utilities and are never downloaded. Windows uses
PowerShell 5.1+, Windows Forms, Storage cmdlets, BCDEdit, and UAC.

## Limitations and troubleshooting

- The active OS must itself have booted in UEFI mode and permit firmware-variable
  writes. Legacy/CSM boots cannot arm a UEFI BootNext entry.
- Some firmware does not retain a removable-media entry. The settings fallback
  remains available.
- Secure Boot may refuse the current development EFI even after the host has
  selected it; host-side selection does not bypass signature policy.
- A copied launcher is rejected. Run the copy inside this USB's
  `OpenConsole/Host` directory.
- BitLocker may request recovery after firmware boot changes on some systems.
  Save recovery material before opting in.
- `--plan`/`--dry-run` prints `openconsole.host-reboot-plan.v1` JSON and never
  changes firmware or restarts the machine.

To remove the feature, delete `OpenConsole/Host`; no service, scheduled task,
registry autorun, desktop entry, or package was installed. Reformatting the USB
also removes it completely.

## VM acceptance

`make test-bootnext-ovmf` uses a persistent OVMF variable store and real UEFI
boot entries. It places the normal-host sentinel first in `BootOrder`, arms the
OpenConsole parent through `BootNext`, and verifies that the parent reaches its
dashboard exactly once. A second cold VM launch must reach the normal sentinel,
find no remaining `BootNext`, and observe the identical persistent `BootOrder`.
This is firmware-level VM evidence, not a claim about every physical motherboard.
