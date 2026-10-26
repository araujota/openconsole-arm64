# OpenConsole removable-media layout 1.0

Installers derive their plan from the device's reported byte capacity and
logical-sector size. Devices through 8 GiB use one aligned FAT32 EFI System
Partition. Larger devices use GPT with a 1 GiB FAT32 ESP (`OC_BOOT`) and a
second FAT32 partition (`OC_DATA`) occupying the remaining aligned usable
space. The boot partition remains deliberately modest for firmware
compatibility; the data partition gives games the rest of the drive.

The data filesystem contains the unchanged mutable tree:

```text
EFI/OpenConsole/
├── data-volume.json
├── State/Games/<id>/...
├── Logs/
└── Notices/
```

`data-volume.json` is canonical ASCII JSON with schema
`openconsole.data-volume.v1` and one UUIDv4 installation identifier. It is a
routing marker, not an authorization credential. Games and executable drivers
remain subject to their existing signatures, platform inventory, confined
paths, transaction records, and delete policy.

If no unique marked data filesystem is visible, the parent uses its boot
filesystem exactly as before. Multiple matching data filesystems disable
mutable operations instead of selecting by firmware handle order.

Both volumes are FAT32. Total capacity may be much larger, but no individual
file can reach 4 GiB; the maximum accepted file size is 4,294,967,295 bytes.
This does not change OCG2, `game.toml`, game-relative paths, or the game ABI.
