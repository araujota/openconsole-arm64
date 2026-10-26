#!/usr/bin/env python3
import importlib.util, pathlib, sys, tempfile
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT=pathlib.Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location("platform_inventory",ROOT/"tools/platform_inventory.py")
assert SPEC and SPEC.loader
mod=importlib.util.module_from_spec(SPEC); sys.modules[SPEC.name]=mod; SPEC.loader.exec_module(mod)

def reject(code,call):
    try: call()
    except mod.InventoryError as exc: assert exc.code==code,(exc.code,code)
    else: raise AssertionError(code)

def main():
    key=Ed448PrivateKey.from_private_bytes(bytes(range(57,0,-1)))
    with tempfile.TemporaryDirectory() as td:
        root=pathlib.Path(td); (root/"EFI/BOOT").mkdir(parents=True); (root/"EFI/OpenConsole/Drivers/Input").mkdir(parents=True)
        (root/"EFI/BOOT/BOOTX64.EFI").write_bytes(b"MZparent")
        (root/"EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi").write_bytes(b"MZmouse")
        specs=[("parent","EFI/BOOT/BOOTX64.EFI","0.1.0",1,True),
               ("input","EFI/OpenConsole/Drivers/Input/UsbMouseDxe.efi","0.1.0",2,True)]
        a=mod.build(root,"development","0.1.0",True,specs,key); b=mod.build(root,"development","0.1.0",True,list(reversed(specs)),key)
        assert a==b
        inv=mod.decode(a,key.public_key(),root); assert inv.signature_verified and len(inv.records)==2
        bad=bytearray(a); bad[128]^=1; reject("OCP1_BAD_SIGNATURE",lambda:mod.decode(bytes(bad),key.public_key()))
        (root/"EFI/BOOT/BOOTX64.EFI").write_bytes(b"changed")
        reject("OCP1_FILE_DIGEST",lambda:mod.decode(a,key.public_key(),root))
        reject("OCP1_BAD_PATH",lambda:mod.path_valid("games/peer/game.efi"))
    print("PASS: canonical signed platform inventory and file revalidation")

if __name__=="__main__": main()
