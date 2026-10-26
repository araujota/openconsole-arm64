#!/usr/bin/env python3
import os
import shutil
import sys


def link_or_copy(source, destination):
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: prepare-pointer-pagination-esp.py PARENT CHILD JACKET OUTPUT")
    parent, child, jacket, output = map(os.path.abspath, sys.argv[1:])
    if os.path.exists(output):
        shutil.rmtree(output)
    os.makedirs(os.path.join(output, "EFI", "BOOT"))
    games = os.path.join(output, "games")
    os.makedirs(games)
    link_or_copy(parent, os.path.join(output, "EFI", "BOOT", "BOOTX64.EFI"))
    for index in range(12):
        game = os.path.join(games, f"library-{index:02d}")
        os.makedirs(game)
        with open(os.path.join(game, "game.toml"), "w", encoding="ascii", newline="\n") as stream:
            stream.write(f'id = "library_{index:02d}"\n')
            stream.write(f'title = "Library Game {index:02d}"\n')
            stream.write('entry = "game.efi"\n')
            stream.write('architecture = "x64"\n')
            stream.write('required_protocols = []\n')
            stream.write('graphics_api = "gop"\n')
            stream.write('accel = false\n')
            stream.write('min_graphics_abi = 0\n')
        link_or_copy(child, os.path.join(game, "game.efi"))
        link_or_copy(jacket, os.path.join(game, "jacket.ocj"))


if __name__ == "__main__":
    main()
