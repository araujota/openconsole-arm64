#!/usr/bin/env python3
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "sdk-examples"
BUILD.mkdir(parents=True, exist_ok=True)


def run(command):
    subprocess.run(command, cwd=ROOT, check=True)


run(["clang", "--target=x86_64-unknown-windows", "-std=c11", "-Wall", "-Wextra",
     "-Werror", "-ffreestanding", "-fshort-wchar", "-fno-stack-protector", "-mno-red-zone",
     "-mno-stack-arg-probe", "-Iinclude", "-Iexamples/sdk/c", "-c",
     "examples/sdk/c/package_root.c", "-o", str(BUILD / "package_root.obj")])
run(["clang", "--target=x86_64-unknown-windows", "-std=c11", "-Wall", "-Wextra",
     "-Werror", "-ffreestanding", "-fshort-wchar", "-fno-stack-protector", "-mno-red-zone",
     "-mno-stack-arg-probe", "-Iinclude", "-Iexamples/sdk/c", "-c",
     "examples/sdk/c/minimal_game.c", "-o", str(BUILD / "minimal_game.obj")])
run(["zig", "build-exe", "examples/sdk/zig/abi_layout.zig", "-femit-bin=" + str(BUILD / "zig-abi")])
run([str(BUILD / "zig-abi")])
run(["rustc", "--edition=2021", "examples/sdk/rust/abi_layout.rs", "-o", str(BUILD / "rust-abi")])
run([str(BUILD / "rust-abi")])
print("PASS: portable C package-root source and independent Zig/Rust ABI examples")
