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
    if len(sys.argv) != 6:
        raise SystemExit("usage: prepare-game-details-esp.py PARENT CHILD JACKET NETWORK_DIR OUTPUT")
    parent, child, jacket, network_dir, output = map(os.path.abspath, sys.argv[1:])
    if os.path.exists(output):
        shutil.rmtree(output)
    os.makedirs(os.path.join(output, "EFI", "BOOT"))
    driver_dir = os.path.join(output, "EFI", "OpenConsole", "Drivers", "Network")
    os.makedirs(driver_dir)
    games = os.path.join(output, "games")
    os.makedirs(games)
    link_or_copy(parent, os.path.join(output, "EFI", "BOOT", "BOOTX64.EFI"))
    for name in os.listdir(network_dir):
        if name.lower().endswith(".efi"):
            link_or_copy(os.path.join(network_dir, name), os.path.join(driver_dir, name))
    for index in range(12):
        game_id = f"library_{index:02d}"
        game = os.path.join(games, game_id)
        os.makedirs(game)
        with open(os.path.join(game, "game.toml"), "w", encoding="ascii", newline="\n") as stream:
            stream.write(f'id = "{game_id}"\n')
            stream.write(f'title = "Game {index:02d}"\n')
            stream.write('entry = "game.efi"\n')
            stream.write('architecture = "x64"\n')
            stream.write('required_protocols = []\n')
            stream.write('graphics_api = "gles2"\n' if index == 11 else 'graphics_api = "gop"\n')
            stream.write('accel = true\n' if index == 11 else 'accel = false\n')
            stream.write('min_graphics_abi = 1\n' if index == 11 else 'min_graphics_abi = 0\n')
            stream.write(f'developer = "OpenConsole Studio {index:02d}"\n')
            stream.write(f'description = "A deterministic EFI game package used to verify details, compatibility, launch, and safely confined deletion for library entry {index:02d}."\n')
            stream.write(f'version = "1.0.{index}"\n')
            stream.write(f'website = "https://games.openconsole.test/{game_id}"\n')
        link_or_copy(child, os.path.join(game, "game.efi"))
        link_or_copy(jacket, os.path.join(game, "jacket.ocj"))
        if index == 0:
            assets = os.path.join(game, "assets", "nested")
            os.makedirs(assets)
            with open(os.path.join(assets, "delete-proof.bin"), "wb") as stream:
                stream.write(b"openconsole-confined-delete-fixture")
    malicious = os.path.join(games, "malicious_target")
    os.makedirs(malicious)
    with open(os.path.join(malicious, "game.toml"), "w", encoding="ascii") as stream:
        stream.write('id = "../escape"\n')
        stream.write('title = "Must Not Load"\n')
        stream.write('entry = "game.efi"\n')
        stream.write('architecture = "x64"\n')
        stream.write('required_protocols = []\n')
        stream.write('graphics_api = "gop"\n')
        stream.write('accel = false\n')
        stream.write('min_graphics_abi = 0\n')
    with open(os.path.join(malicious, "do-not-delete.txt"), "wb") as stream:
        stream.write(b"malicious-target-rejection-proof")


if __name__ == "__main__":
    main()
