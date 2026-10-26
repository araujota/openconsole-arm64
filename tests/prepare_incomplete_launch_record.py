#!/usr/bin/env python3
"""Write a deterministic incomplete launch record for OVMF recovery tests."""
import pathlib, struct, sys, zlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"));import ocg2
esp=pathlib.Path(sys.argv[1]);bundle=ocg2.decode((ROOT/"build/authenticated-game-fixture/game.ocg").read_bytes())
out=bytearray(196);out[:4]=b"OCLR";out[4]=1;out[5]=1
struct.pack_into("<Q",out,8,1);out[16:48]=bundle.bundle_id
gid=bundle.game_id.encode("ascii");version=bundle.version.encode("ascii")
out[48:50]=bytes((len(gid),len(version)));out[56:56+len(gid)]=gid;out[120:120+len(version)]=version
struct.pack_into("<I",out,192,zlib.crc32(out[:192])&0xffffffff)
target=esp/"EFI/OpenConsole/State/Launch";target.mkdir(parents=True,exist_ok=True)
(target/"attempt-a.oclr").write_bytes(out)
