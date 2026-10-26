#!/usr/bin/env python3
from __future__ import annotations
import pathlib, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="oc-fat32-") as directory:
    base=pathlib.Path(directory); binary=base/"format"; image=base/"volume.img"
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-O2",str(ROOT/"tools/oc-fat32-format.c"),"-o",str(binary)],check=True)
    with image.open("wb") as stream: stream.truncate(256*1024*1024)
    formatted=subprocess.run([str(binary),str(image),"OC_DATA","512"],text=True,capture_output=True,check=True)
    assert "FAT32" in formatted.stdout and "clusterSectors=" in formatted.stdout
    result=subprocess.run(["fsck.fat","-vn",str(image)],text=True,capture_output=True,check=True)
    assert "files, 1/" in result.stdout
print("PASS: repository-owned FAT32 formatter produces a clean filesystem")
