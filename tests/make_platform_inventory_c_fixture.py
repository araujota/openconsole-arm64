#!/usr/bin/env python3
import pathlib,sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/"tools"));import platform_inventory as ocp
out=ROOT/"build/platform-inventory-c-fixture";root=out/"root";(root/"EFI/BOOT").mkdir(parents=True,exist_ok=True)
parent=b"MZ deterministic parent fixture";(root/"EFI/BOOT/BOOTX64.EFI").write_bytes(parent)
key=Ed448PrivateKey.from_private_bytes(bytes(range(1,58)));public=key.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
(out/"platform.pub").write_bytes(public)
blob=ocp.build(root,"development","0.1.0",True,[("parent","EFI/BOOT/BOOTX64.EFI","0.1.0",1,True)],key)
(out/"platform.ocp").write_bytes(blob)
