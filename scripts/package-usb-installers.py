#!/usr/bin/env python3
"""Create deterministic architecture-labelled OpenConsole USB installer kits."""
from __future__ import annotations
import argparse,hashlib,json,pathlib,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
COMMON=("scripts/install-openconsole-linux.py","scripts/install-openconsole-windows.ps1",
        "tools/oc-fat32-format.c","docs/installing-openconsole-usb.md",
        "docs/removable-media-layout-v1.md","docs/windows-fat32-formatter.md")
def add(z:zipfile.ZipFile,name:str,data:bytes,executable:bool=False)->None:
    info=zipfile.ZipInfo(name,(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
    info.external_attr=((0o755 if executable else 0o644)&0xffff)<<16
    z.writestr(info,data)
def package(arch:str,payload:pathlib.Path,out:pathlib.Path,formatter:pathlib.Path|None)->dict:
    boot="BOOTX64.EFI" if arch=="x64" else "BOOTAA64.EFI"
    if not (payload/"EFI/BOOT"/boot).is_file() or not (payload/"EFI/OpenConsole/platform.ocp").is_file():
        raise ValueError(f"{arch} payload lacks {boot} or platform.ocp")
    out.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(out,"w") as z:
        prefix=f"openconsole-usb-{arch}"
        for source in sorted(item for item in payload.rglob("*") if item.is_file()):
            add(z,f"{prefix}/payload/{source.relative_to(payload).as_posix()}",source.read_bytes())
        # Packaging synthetic or externally assembled payloads still gets the
        # exact baseline host launchers; an existing staged file must match.
        for source in sorted(item for item in (ROOT/"host-tools").iterdir() if item.is_file()):
            name=f"{prefix}/payload/OpenConsole/Host/{source.name}"
            existing=payload/"OpenConsole/Host"/source.name
            if existing.is_file() and existing.read_bytes()!=source.read_bytes():
                raise ValueError(f"stale staged host tool: {source.name}")
            if not existing.is_file(): add(z,name,source.read_bytes(),source.suffix in (".py",".sh"))
        for relative in COMMON:
            add(z,f"{prefix}/{relative}",(ROOT/relative).read_bytes(),relative.endswith(".py"))
        if formatter and formatter.is_file():
            add(z,f"{prefix}/build/host-tools/oc-fat32-format.exe",formatter.read_bytes(),True)
        manifest={"schema":"openconsole.usb-installer-kit.v1","architecture":arch,
                  "bootFilename":boot,"physicalUsbValidation":"unverified"}
        add(z,f"{prefix}/installer-kit.json",(json.dumps(manifest,sort_keys=True,separators=(",",":"))+"\n").encode())
    return {"architecture":arch,"path":str(out),"bytes":out.stat().st_size,
            "sha256":hashlib.sha256(out.read_bytes()).hexdigest()}
def main()->int:
    p=argparse.ArgumentParser();p.add_argument("--x64-payload",type=pathlib.Path,required=True)
    p.add_argument("--arm64-payload",type=pathlib.Path,required=True);p.add_argument("--output",type=pathlib.Path,default=ROOT/"build/usb-installers")
    p.add_argument("--windows-formatter",type=pathlib.Path);a=p.parse_args()
    reports=[package(arch,payload.resolve(),a.output/f"openconsole-usb-{arch}.zip",a.windows_formatter)
             for arch,payload in (("x64",a.x64_payload),("arm64",a.arm64_payload))]
    print(json.dumps({"schema":"openconsole.usb-installer-packages.v1","packages":reports},sort_keys=True));return 0
if __name__=="__main__":raise SystemExit(main())
