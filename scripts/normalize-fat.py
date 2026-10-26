#!/usr/bin/env python3
"""Normalize FAT directory timestamps without changing file data or layout."""

import argparse
import pathlib
import struct

FIXED_DATE = ((2024 - 1980) << 9) | (1 << 5) | 1


def normalize(path: pathlib.Path) -> None:
    data = bytearray(path.read_bytes())
    bps = struct.unpack_from("<H", data, 11)[0]
    spc, reserved, fats = data[13], struct.unpack_from("<H", data, 14)[0], data[16]
    fat_size = struct.unpack_from("<I", data, 36)[0]
    root = struct.unpack_from("<I", data, 44)[0]
    if bps != 512 or not spc or not fat_size or root < 2:
        raise ValueError("only canonical FAT32 images are supported")
    fat_at = reserved * bps
    data_at = (reserved + fats * fat_size) * bps

    def next_cluster(cluster: int) -> int:
        return struct.unpack_from("<I", data, fat_at + cluster * 4)[0] & 0x0FFFFFFF

    def chain(cluster: int):
        seen = set()
        while 2 <= cluster < 0x0FFFFFF8:
            if cluster in seen:
                raise ValueError("FAT cluster loop")
            seen.add(cluster)
            yield cluster
            cluster = next_cluster(cluster)

    visited = set()

    def directory(cluster: int) -> None:
        if cluster in visited:
            return
        visited.add(cluster)
        children = []
        for item in chain(cluster):
            base = data_at + (item - 2) * spc * bps
            for offset in range(base, base + spc * bps, 32):
                first, attr = data[offset], data[offset + 11]
                if first == 0x00:
                    break
                if first == 0xE5 or attr == 0x0F:
                    continue
                data[offset + 13] = 0
                struct.pack_into("<HHHHHH", data, offset + 14, 0, FIXED_DATE, FIXED_DATE,
                                 struct.unpack_from("<H", data, offset + 20)[0], 0, FIXED_DATE)
                name = bytes(data[offset:offset + 11])
                child = (struct.unpack_from("<H", data, offset + 20)[0] << 16) | struct.unpack_from("<H", data, offset + 26)[0]
                if attr & 0x10 and name not in (b".          ", b"..         ") and child >= 2:
                    children.append(child)
        for child in children:
            directory(child)

    directory(root)
    path.write_bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=pathlib.Path)
    args = parser.parse_args()
    normalize(args.image)
