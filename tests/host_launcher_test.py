#!/usr/bin/env python3
from __future__ import annotations
import importlib.util,json,pathlib,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("openconsole_host",ROOT/"host-tools/openconsole_host.py")
mod=importlib.util.module_from_spec(spec);sys.modules[spec.name]=mod;spec.loader.exec_module(mod)

def reject(fn):
    try: fn()
    except mod.HostError: return
    raise AssertionError("unsafe input was accepted")

with tempfile.TemporaryDirectory(prefix="oc-host-") as td:
    root=pathlib.Path(td)/"media";host=root/"OpenConsole/Host";(root/"EFI/BOOT").mkdir(parents=True);(root/"EFI/OpenConsole").mkdir(parents=True);host.mkdir(parents=True)
    (root/"EFI/BOOT/BOOTX64.EFI").write_bytes(b"efi");(root/"EFI/OpenConsole/platform.ocp").write_bytes(b"ocp")
    ident="12345678-1234-4234-9234-123456789abc"
    (root/"EFI/OpenConsole/data-volume.json").write_text('{"installationId":"'+ident+'","schema":"openconsole.data-volume.v1"}\n')
    (host/"manifest.json").write_bytes((ROOT/"host-tools/manifest.json").read_bytes())
    part="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"
    tree={"blockdevices":[{"name":"/dev/sdz","path":"/dev/sdz","type":"disk","rm":True,"tran":"usb","serial":"S","mountpoints":None,"children":[{"name":"/dev/sdz1","path":"/dev/sdz1","type":"part","pkname":"/dev/sdz","partuuid":part,"mountpoints":[str(root)]}]}]}
    media=mod.discover(host,tree,"x86_64");assert media.installation_id==ident and media.boot_partuuid==part
    text=f"Boot0001* Normal HD(1,GPT,{part},0x800,0x1000)/File(\\EFI\\OTHER.EFI)\nBoot00AF* Anything HD(1,GPT,{part},0x800,0x1000)/File(\\EFI\\BOOT\\BOOTX64.EFI)\nBootNext: 00AF\n"
    entries,next_value=mod.parse_efibootmgr(text,part,"\\EFI\\BOOT\\BOOTX64.EFI")
    assert entries==["00AF"] and next_value=="00AF"
    plan=mod.make_plan(media,entries,next_value);assert plan["action"]=="boot-next" and not plan["persistentBootOrderMutation"]
    calls=[]
    mod.arm_bootnext(media,"00AF",media.token,text,lambda c,check=True:calls.append((c,check)),lambda:text)
    assert calls==[(["efibootmgr","--bootnext","00AF"],True)]
    calls=[]
    reject(lambda:mod.arm_bootnext(media,"00AF",media.token,text,lambda c,check=True:calls.append((c,check)),lambda:text.replace("BootNext: 00AF","BootNext: 0001")))
    assert calls[-1]==(["efibootmgr","--bootnext","00AF"],False)
    calls=[];reject(lambda:mod.arm_bootnext(media,"00AF","stale",text,lambda c,check=True:calls.append((c,check)),lambda:text));assert calls==[]
    assert mod.parse_efibootmgr(text.replace(part,"ffffffff-ffff-4fff-8fff-ffffffffffff"),part,media.boot_path)[0]==[]
    reject(lambda: mod.discover(host,tree,"aarch64"))
    copied=root/"copied";copied.mkdir();reject(lambda: mod.discover(copied,tree,"x86_64"))
    (root/"EFI/BOOT/BOOTAA64.EFI").write_bytes(b"efi");reject(lambda: mod.discover(host,tree,"x86_64"));(root/"EFI/BOOT/BOOTAA64.EFI").unlink()
    marker=root/"EFI/OpenConsole/data-volume.json";marker.write_bytes(b"x"*513);reject(lambda: mod.discover(host,tree,"x86_64"))
print("PASS: bounded host media discovery and exact EFI entry matching")
