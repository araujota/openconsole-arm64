#!/usr/bin/env python3
import pathlib,sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/"tools"));import ocg2,publisher_keys
out=ROOT/"build/authenticated-game-fixture";out.mkdir(parents=True,exist_ok=True)
manifest=b'''format_version = "2.0"
package_format = "ocg2"
id = "authenticated"
title = "Authenticated fixture"
version = "1.0.0"
entry = "bin/game.efi"
architecture = "x64"
required_capabilities = ["uefi.input.keyboard@1.0"]
optional_capabilities = []
graphics_api = "gop"
accel = "forbidden"
min_graphics_abi = 0
'''
key=Ed448PrivateKey.from_private_bytes(bytes(range(1,58)));raw=key.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw);(out/"publisher.pub").write_bytes(raw);(out/"game.toml").write_bytes(manifest)
(out/"game.ocg").write_bytes(ocg2.pack(out/"game.toml",{"entry-x64":ROOT/"build/INPUT-PROBE.EFI"},key))
(out/"publishers.ocpk").write_bytes(publisher_keys.build([("openconsole","OpenConsole test publisher",out/"publisher.pub",1)]))
(out/"empty.ocpk").write_bytes(publisher_keys.build([]))
