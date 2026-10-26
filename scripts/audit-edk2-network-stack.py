#!/usr/bin/env python3
"""Audit the exact pinned EDK II network EFI package and linked file closure."""

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

PIN = "0a503bc2f5bd37c7a5d39d0ba31008f446f0298d"
MODULES = ("DpcDxe", "MnpDxe", "ArpDxe", "Dhcp4Dxe", "Ip4Dxe",
           "Udp4Dxe", "Hash2DxeCrypto", "TcpDxe", "DnsDxe", "TlsDxe",
           "HttpUtilitiesDxe", "HttpDxe")
SPDX = re.compile(r"SPDX-License-Identifier:\s*([^*\r\n]+)")
DEPENDENCY_PATH = re.compile(r"(?P<path>/[^\\\r\n ]+)")


def run(*command):
    return subprocess.run(command, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def license_basis(relative, source):
    try:
        text = source.read_text(encoding="utf-8", errors="replace")[:16384]
    except OSError:
        return None
    match = SPDX.search(text)
    if match:
        return match.group(1).strip()
    if relative.startswith("CryptoPkg/Library/OpensslLib/openssl/"):
        return "Apache-2.0 (OpenSSL LICENSE.txt)"
    return "BSD-2-Clause-Patent (EDK II License.txt; no file SPDX tag)"


def module_output(build_root, module):
    matches = list(build_root.glob(
        f"*/RELEASE_GCC/X64/*/{module}/{module}/OUTPUT"))
    if len(matches) != 1:
        raise RuntimeError(f"cannot resolve build output for {module}")
    return matches[0]


def linked_dependencies(source_root, build_root, module):
    output = module_output(build_root, module)
    dependency_files = list(output.glob("*.obj.deps"))
    libraries = output / "static_library_files.lst"
    if not libraries.is_file():
        raise RuntimeError(f"missing linker library list for {module}")
    for raw in libraries.read_text(encoding="utf-8").splitlines():
        library = pathlib.Path(raw.strip())
        if not library.is_file():
            raise RuntimeError(f"missing linked library for {module}: {library}")
        dependency_files.extend(library.parent.rglob("*.obj.deps"))
    closure = set()
    source_prefix = str(source_root.resolve()) + os.sep
    for dependency in dependency_files:
        text = dependency.read_text(encoding="utf-8", errors="replace")
        for match in DEPENDENCY_PATH.finditer(text):
            path = match.group("path")
            if not path.startswith(source_prefix) or "/Build/" in path:
                continue
            candidate = pathlib.Path(path)
            if candidate.is_file():
                closure.add(candidate.relative_to(source_root).as_posix())
    if not closure:
        raise RuntimeError(f"empty linked-file closure for {module}")
    return closure


def audit_binary(path):
    pe = run("objdump", "-p", str(path))
    symbols = run("objdump", "-t", str(path))
    if "EFI boot service driver" not in pe:
        raise RuntimeError(f"{path.name} is not an EFI boot-service driver")
    import_entry = next((line for line in pe.splitlines()
                         if "Import Directory [parts of .idata]" in line), "")
    if not import_entry or not re.search(r"Entry 1\s+0+\s+0+", import_entry):
        raise RuntimeError(f"{path.name} has a non-empty PE import directory")
    undefined = [line for line in symbols.splitlines() if "*UND*" in line]
    if undefined:
        raise RuntimeError(f"{path.name} has undefined symbols: {undefined[:3]}")
    return {"bytes": path.stat().st_size, "sha256": sha256(path),
            "pe_import_directory_empty": True, "undefined_symbols": 0,
            "subsystem": "EFI boot service driver"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--package", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--runtime", required=True, type=pathlib.Path)
    parser.add_argument("--runtime-log", required=True, type=pathlib.Path)
    parser.add_argument("--firmware", required=True, type=pathlib.Path)
    args = parser.parse_args()
    source = args.source.resolve()
    package = args.package.resolve()
    if run("git", "-C", str(source), "rev-parse", "HEAD").strip() != PIN:
        raise RuntimeError("EDK II checkout does not match the audited pin")
    commit_file = package / "EDK2_COMMIT"
    if not commit_file.is_file() or commit_file.read_text().strip() != PIN:
        raise RuntimeError("package commit marker does not match the audited pin")
    build_root = source / "Build"
    result = {"schema": 2, "edk2_commit": PIN, "drivers": {}, "files": {}}
    all_files = set()
    for module in MODULES:
        binary = package / f"{module}.efi"
        if not binary.is_file():
            raise RuntimeError(f"missing packaged driver {binary.name}")
        result["drivers"][module] = audit_binary(binary)
        closure = linked_dependencies(source, build_root, module)
        result["drivers"][module]["linked_file_count"] = len(closure)
        result["drivers"][module]["linked_files"] = sorted(closure)
        all_files.update(closure)
    for relative in sorted(all_files):
        basis = license_basis(relative, source / relative)
        if basis is None:
            raise RuntimeError(f"cannot establish license basis for {relative}")
        result["files"][relative] = {"sha256": sha256(source / relative),
                                      "license": basis}
    runtime = json.loads(args.runtime.read_text(encoding="utf-8"))
    if runtime.get("network_stack") != "packaged-edk2":
        raise RuntimeError("runtime metrics do not identify the packaged EDK II stack")
    if runtime.get("provider_live_bytes_after_cleanup") != [0, 0] or \
       runtime.get("provider_live_allocations_after_cleanup") != [0, 0]:
        raise RuntimeError("runtime allocation teardown is incomplete")
    latencies = runtime.get("operation_latency_usec", {})
    required_operations = ("dhcp", "dns", "udp", "http", "bounds", "timeout")
    if any(len(latencies.get(name, [])) != 2 for name in required_operations):
        raise RuntimeError("runtime request-latency evidence is incomplete")
    runtime_log = args.runtime_log.read_text(encoding="utf-8", errors="replace")
    if runtime_log.count("OPENCONSOLE_NET_STACK_PACKAGED_LOADED") != 1:
        raise RuntimeError("runtime log does not prove one packaged-stack load")
    if runtime_log.count("OPENCONSOLE_NET_GENERATION_CLEAN") != 2:
        raise RuntimeError("runtime log does not prove two clean child generations")
    firmware = args.firmware.resolve()
    if not firmware.is_file():
        raise RuntimeError("SNP-only firmware image is missing")
    result["runtime"] = runtime
    result["runtime"]["debug_log_sha256"] = sha256(args.runtime_log)
    result["runtime"]["snp_only_firmware"] = {
        "bytes": firmware.stat().st_size, "sha256": sha256(firmware)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    total = sum(item["bytes"] for item in result["drivers"].values())
    print(f"PASS: {len(MODULES)} EFI drivers, {total} bytes, "
          f"{len(all_files)} unique linked files, packaged-stack runtime and teardown")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
