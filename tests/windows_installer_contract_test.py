#!/usr/bin/env python3
from __future__ import annotations
import pathlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
text=(ROOT/"scripts/install-openconsole-windows.ps1").read_text()
required=("Get-Disk","IsSystem","IsBoot","identityToken","Clear-Disk","Initialize-Disk",
          "PartitionStyle GPT","OC_BOOT","OC_DATA","oc-fat32-format.exe",
          "openconsole.data-volume.v1","BOOTX64.EFI","BOOTAA64.EFI",
          "openconsole.usb-install-report.v1","TestVirtualDisk","Get-TreeDigest",
          "Payload byte verification failed","TestFault")
assert all(token in text for token in required)
assert text.index("identity changed") < text.index("Clear-Disk")
assert text.index("system/boot disk") < text.index("Clear-Disk")
print("PASS: Windows installer statically gates mutation behind system-disk and identity checks")
