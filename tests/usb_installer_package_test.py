#!/usr/bin/env python3
from __future__ import annotations
import hashlib,pathlib,subprocess,sys,tempfile,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="oc-installer-kit-") as d:
    base=pathlib.Path(d);payloads=[]
    for arch,boot in (("x64","BOOTX64.EFI"),("arm64","BOOTAA64.EFI")):
        payload=base/arch;(payload/"EFI/BOOT").mkdir(parents=True);(payload/"EFI/OpenConsole").mkdir(parents=True)
        (payload/"EFI/BOOT"/boot).write_bytes(arch.encode());(payload/"EFI/OpenConsole/platform.ocp").write_bytes(b"signed")
        payloads.append(payload)
    out1=base/"one";out2=base/"two"
    command=[sys.executable,"scripts/package-usb-installers.py","--x64-payload",str(payloads[0]),"--arm64-payload",str(payloads[1])]
    subprocess.run(command+["--output",str(out1)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    subprocess.run(command+["--output",str(out2)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    for arch,boot in (("x64","BOOTX64.EFI"),("arm64","BOOTAA64.EFI")):
        left=out1/f"openconsole-usb-{arch}.zip";right=out2/left.name
        assert hashlib.sha256(left.read_bytes()).digest()==hashlib.sha256(right.read_bytes()).digest()
        with zipfile.ZipFile(left) as archive:
            names=set(archive.namelist());prefix=f"openconsole-usb-{arch}"
            assert f"{prefix}/payload/EFI/BOOT/{boot}" in names
            assert f"{prefix}/scripts/install-openconsole-linux.py" in names
            assert f"{prefix}/scripts/install-openconsole-windows.ps1" in names
            assert f"{prefix}/tools/oc-fat32-format.c" in names
            assert f"{prefix}/payload/OpenConsole/Host/manifest.json" in names
            assert f"{prefix}/payload/OpenConsole/Host/Open OpenConsole on next reboot.cmd" in names
print("PASS: deterministic architecture-labelled USB installer kits")
