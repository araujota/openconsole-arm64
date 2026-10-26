#!/usr/bin/env python3
"""Bounded deterministic static analyzer for OpenConsole OCG2 bundles."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import sys
import time
from dataclasses import dataclass
from typing import Any, Iterable

import game_assets
import ocg2

try:
    import capstone
except ImportError:  # A missing decoder is an explicit inconclusive outcome.
    capstone = None

ANALYZER_VERSION = "0.1.0"
RULESET_VERSION = "2026-10-25.1"
SCHEMA = "openconsole.analyzer.report.v1"
MAX_FINDINGS = 256
MAX_EXECUTABLE_BYTES = 8 * 1024 * 1024
MAX_EXECUTABLE_SECTIONS = 32
DEFAULT_DEADLINE_SECONDS = 5.0
SUPPORTED_CAPABILITIES = {
    "uefi.input.keyboard@1.0", "uefi.input.pointer@1.0",
    "openconsole.gles2@0.1", "openconsole.net@0.1", "openconsole.audio@0.2",
}

SEVERITY_ORDER = {"info": 0, "low": 1, "medium": 2, "high": 3, "critical": 4}
EFFECT_ORDER = {"none": 0, "review": 1, "inconclusive": 2, "block": 3}


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    characteristics: int


class AnalysisLimit(RuntimeError):
    pass


def _evidence(data: bytes) -> str:
    return hashlib.sha256(data[:32]).hexdigest()


def finding(rule_id: str, severity: str, confidence: str, effect: str,
            explanation: str, remediation: str, *, file: str = "<bundle>",
            file_offset: int | None = None, virtual_address: int | None = None,
            evidence: bytes = b"") -> dict[str, Any]:
    return {
        "ruleId": rule_id,
        "severity": severity,
        "confidence": confidence,
        "publicationEffect": effect,
        "file": file,
        "fileOffset": file_offset,
        "virtualAddress": virtual_address,
        "evidenceHash": _evidence(evidence),
        "explanation": explanation,
        "remediation": remediation,
    }


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def parse_pe(data: bytes, architecture: str = "x64") -> tuple[dict[str, int | str], list[Section]]:
    """Parse the same bounded PE32+ EFI application subset as the EFI core."""
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("PE_BAD_DOS")
    pe = _u32(data, 0x3C)
    if pe > len(data) - 24 or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("PE_BAD_SIGNATURE")
    expected_machine = {"x64": 0x8664, "arm64": 0xAA64}.get(architecture)
    if expected_machine is None or _u16(data, pe + 4) != expected_machine:
        raise ValueError("PE_BAD_MACHINE")
    count, optional_size = _u16(data, pe + 6), _u16(data, pe + 20)
    optional = pe + 24
    if not 1 <= count <= 96 or optional_size < 112 or optional > len(data) - optional_size:
        raise ValueError("PE_BAD_OPTIONAL_HEADER")
    if _u16(data, optional) != 0x20B:
        raise ValueError("PE_NOT_PE32_PLUS")
    entry, image_size, headers_size = (_u32(data, optional + 16),
                                       _u32(data, optional + 56),
                                       _u32(data, optional + 60))
    subsystem = _u16(data, optional + 68)
    if subsystem != 10:
        raise ValueError("PE_NOT_EFI_APPLICATION")
    section_table = optional + optional_size
    if section_table > len(data) - count * 40:
        raise ValueError("PE_TRUNCATED_SECTIONS")
    directory_count = _u32(data, optional + 108)
    if directory_count > 16 or optional_size < 112 + directory_count * 8:
        raise ValueError("PE_BAD_DATA_DIRECTORIES")
    directories = [(_u32(data, optional + 112 + index * 8),
                    _u32(data, optional + 116 + index * 8))
                   for index in range(directory_count)]
    sections: list[Section] = []
    entry_found = False
    for index in range(count):
        base = section_table + index * 40
        name = data[base:base + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        virtual_size, virtual_address = _u32(data, base + 8), _u32(data, base + 12)
        raw_size, raw_offset = _u32(data, base + 16), _u32(data, base + 20)
        characteristics = _u32(data, base + 36)
        span = max(virtual_size, raw_size)
        if (raw_size and raw_offset > len(data) - raw_size) or virtual_address > image_size or span > image_size - virtual_address:
            raise ValueError("PE_BAD_SECTION_RANGE")
        if virtual_address <= entry < virtual_address + span and characteristics & 0x20000000:
            entry_found = True
        sections.append(Section(name, virtual_address, virtual_size, raw_offset,
                                raw_size, characteristics))
    if headers_size > len(data) or not entry_found:
        raise ValueError("PE_BAD_ENTRY_POINT")
    for left_index, left in enumerate(sections):
        left_virtual_end = left.virtual_address + max(left.virtual_size, left.raw_size)
        left_raw_end = left.raw_offset + left.raw_size
        for right in sections[left_index + 1:]:
            right_virtual_end = right.virtual_address + max(right.virtual_size, right.raw_size)
            right_raw_end = right.raw_offset + right.raw_size
            if (left.virtual_address < right_virtual_end and
                    right.virtual_address < left_virtual_end):
                raise ValueError("PE_OVERLAPPING_VIRTUAL_SECTIONS")
            if (left.raw_size and right.raw_size and left.raw_offset < right_raw_end and
                    right.raw_offset < left_raw_end):
                raise ValueError("PE_OVERLAPPING_RAW_SECTIONS")

    def map_directory(index: int, label: str) -> tuple[int, int] | None:
        if index >= len(directories):
            return None
        rva, size = directories[index]
        if (rva == 0) != (size == 0):
            raise ValueError(f"PE_BAD_{label}_DIRECTORY")
        if not rva:
            return None
        matches = []
        for section in sections:
            if section.virtual_address <= rva and rva - section.virtual_address <= section.raw_size:
                delta = rva - section.virtual_address
                if size <= section.raw_size - delta:
                    matches.append(section.raw_offset + delta)
        if len(matches) != 1:
            raise ValueError(f"PE_BAD_{label}_DIRECTORY")
        return matches[0], size

    imports = map_directory(1, "IMPORT")
    relocations = map_directory(5, "RELOCATION")
    if imports is not None and imports[1] < 20:
        raise ValueError("PE_BAD_IMPORT_DIRECTORY")
    if relocations is not None and relocations[1] < 8:
        raise ValueError("PE_BAD_RELOCATION_DIRECTORY")
    return {"machine": expected_machine, "architecture": architecture,
            "subsystem": subsystem, "entryRva": entry,
            "imageSize": image_size, "sectionCount": count,
            "importBytes": 0 if imports is None else imports[1],
            "relocationBytes": 0 if relocations is None else relocations[1]}, sections


def _locations(data: bytes, needle: bytes) -> Iterable[int]:
    start = 0
    while True:
        position = data.find(needle, start)
        if position < 0:
            return
        yield position
        start = position + 1


def scan_executable(path: str, data: bytes, sections: list[Section],
                    required_capabilities: set[str], deadline: float,
                    architecture: str = "x64") -> list[dict[str, Any]]:
    executable = [section for section in sections if section.characteristics & 0x20000000]
    if len(executable) > MAX_EXECUTABLE_SECTIONS:
        raise AnalysisLimit("executable section count exceeds budget")
    if sum(section.raw_size for section in executable) > MAX_EXECUTABLE_BYTES:
        raise AnalysisLimit("executable byte count exceeds budget")
    results: list[dict[str, Any]] = []

    def add(rule: str, severity: str, confidence: str, effect: str,
            explanation: str, remediation: str, offset: int, va: int, width: int) -> None:
        if len(results) >= MAX_FINDINGS:
            raise AnalysisLimit("finding count exceeds budget")
        results.append(finding(rule, severity, confidence, effect, explanation,
                               remediation, file=path, file_offset=offset,
                               virtual_address=va, evidence=data[offset:offset + width]))

    if capstone is None:
        raise AnalysisLimit(f"the pinned Capstone {architecture} decoder is unavailable")
    if architecture == "x64":
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    elif architecture == "arm64":
        decoder = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_LITTLE_ENDIAN)
    else:
        raise AnalysisLimit("unsupported architecture machine-code policy")
    decoder.skipdata = True
    for section in executable:
        if section.characteristics & 0x80000000:
            add("OCSA-PE-WRITABLE-EXECUTABLE", "critical", "verified", "block",
                "A PE section is simultaneously writable and executable.",
                "Separate mutable data from executable code and relink the EFI application.",
                section.raw_offset, section.virtual_address, min(32, section.raw_size))
        if time.monotonic() > deadline:
            raise AnalysisLimit("analysis deadline expired")
        body = data[section.raw_offset:section.raw_offset + section.raw_size]
        decoded = 0
        skipped = 0
        for instruction in decoder.disasm(body, section.virtual_address):
            decoded += instruction.size
            local = instruction.address - section.virtual_address
            absolute, va = section.raw_offset + local, instruction.address
            mnemonic = instruction.mnemonic.lower()
            if architecture == "arm64" and mnemonic in (
                    "msr", "mrs", "sys", "sysl", "dc", "ic", "tlbi", "at",
                    "hvc", "smc", "eret", "wfi", "wfe"):
                add("OCSA-ARM64-SYSTEM-INSTRUCTION", "critical", "medium", "block",
                    "Decoded ARM64 code contains a system or privileged instruction.",
                    "Use a declared OpenConsole or UEFI protocol instead of direct system control.",
                    absolute, va, instruction.size)
            elif architecture == "x64" and mnemonic in ("in", "out", "ins", "insb", "insd", "insw",
                            "outs", "outsb", "outsd", "outsw"):
                add("OCSA-X64-DIRECT-IO", "critical", "medium", "block",
                    "Decoded x86 code contains a direct port-I/O instruction.",
                    "Use a declared OpenConsole protocol or UEFI protocol instead of direct port I/O.",
                    absolute, va, instruction.size)
            elif architecture == "x64" and mnemonic in ("cli", "sti", "hlt"):
                add("OCSA-X64-PRIVILEGED", "critical", "medium", "block",
                    "Decoded x86 code contains CLI, STI, or HLT.",
                    "Remove privileged processor-state manipulation.", absolute, va, instruction.size)
            elif architecture == "x64" and mnemonic == "mov" and ("cr0" in instruction.op_str or
                                          "cr2" in instruction.op_str or
                                          "cr3" in instruction.op_str or
                                          "cr4" in instruction.op_str or
                                          "cr8" in instruction.op_str):
                add("OCSA-X64-CONTROL-REGISTER", "critical", "medium", "block",
                    "Decoded x86 code accesses a control register.",
                    "Do not read or write x86 control registers from a game.",
                    absolute, va, instruction.size)
            elif mnemonic == ".byte":
                skipped += instruction.size
            elif architecture == "x64" and mnemonic.startswith("mov") and "0x" in instruction.op_str:
                for token in instruction.op_str.replace("[", " ").replace("]", " ").replace(",", " ").split():
                    try:
                        address = int(token, 16) if token.startswith("0x") else 0
                    except ValueError:
                        address = 0
                    if address >= 0xF0000000:
                        add("OCSA-X64-MMIO-ABSOLUTE", "high", "low", "review",
                            "A decoded absolute memory operand resembles a physical MMIO address.",
                            "Use a declared platform protocol; document unavoidable device access.",
                            absolute, va, instruction.size)
                        break
        if decoded != len(body) or skipped > max(16, len(body) // 100):
            raise AnalysisLimit("the decoder could not cover the executable section within policy")

    string_rules = (
        ("SetVariable", "OCSA-UEFI-VARIABLE-WRITE", "critical", "block",
         "Firmware-variable mutation indicator found.", "Games must not mutate firmware variables."),
        ("ResetSystem", "OCSA-UEFI-RESET", "critical", "block",
         "System-reset service indicator found.", "Return to the parent instead of resetting the host."),
        ("WriteFile", "OCSA-FILESYSTEM-WRITE", "high", "block",
         "Filesystem-write indicator found.", "Keep games read-only; use a future parent-mediated save protocol."),
        ("EFI_DRIVER_BINDING_PROTOCOL", "OCSA-DRIVER-BINDING", "critical", "block",
         "Driver-binding protocol indicator found in an application.", "Package drivers separately under platform policy."),
        ("ExitBootServices", "OCSA-LIFECYCLE-EXIT-BOOT-SERVICES", "critical", "block",
         "ExitBootServices lifecycle indicator found.", "A game must return under the parent's existing boot-services lifecycle."),
        ("SetWatchdogTimer", "OCSA-LIFECYCLE-WATCHDOG", "high", "block",
         "Direct watchdog manipulation indicator found.", "Leave watchdog and recovery policy under parent control."),
        ("InstallProtocolInterface", "OCSA-LIFECYCLE-PROTOCOL-INSTALL", "medium", "review",
         "Protocol installation can outlive the game generation.", "Use parent-provided generation-scoped protocols and uninstall owned resources before return."),
        ("LoadImage", "OCSA-LIFECYCLE-IMAGE-LOAD", "high", "review",
         "Nested EFI image loading indicator found.", "Declare and justify child loading; normally return launch control to the parent."),
    )
    for text, rule, severity, effect, explanation, remediation in string_rules:
        variants = (text.encode("ascii"), text.encode("utf-16le"))
        offsets = sorted({offset for variant in variants for offset in _locations(data, variant)})
        for offset in offsets:
            add(rule, severity, "medium", effect, explanation, remediation,
                offset, 0, min(32, len(data) - offset))
    network_markers = (b"EFI_HTTP_PROTOCOL", b"EFI_TCP4_PROTOCOL", b"EFI_UDP4_PROTOCOL",
                       b"http://", b"https://")
    has_network = any(capability.startswith("openconsole.net@") for capability in required_capabilities)
    if not has_network:
        for marker in network_markers:
            for offset in _locations(data.lower(), marker.lower()):
                add("OCSA-NETWORK-UNDECLARED", "high", "medium", "block",
                    "Network-use indicator is present without a required network capability.",
                    "Declare the versioned openconsole.net capability or remove network use.",
                    offset, 0, len(marker))
    device_markers = (b"PciRoot(", b"Pci(", b"Acpi(", b"VenHw(")
    for marker in device_markers:
        for offset in _locations(data, marker):
            add("OCSA-DEVICE-PATH-SUSPICIOUS", "medium", "low", "review",
                "Embedded device-path text can indicate direct hardware targeting.",
                "Prefer a declared OpenConsole or standard UEFI protocol.", offset, 0, len(marker))
    sensitive_guids = ((bytes.fromhex("61dfe48bca93d211aa0d00e098032b8c"),
                        "EFI global-variable namespace GUID"),)
    for marker, label in sensitive_guids:
        for offset in _locations(data, marker):
            add("OCSA-GUID-SENSITIVE", "high", "medium", "review",
                f"Embedded {label} may indicate direct persistent firmware access.",
                "Remove direct global-variable access or document it for explicit review.",
                offset, 0, len(marker))
    return results


def _sort_findings(findings: list[dict[str, Any]]) -> None:
    findings.sort(key=lambda item: (item["ruleId"], item["file"],
                                    -1 if item["fileOffset"] is None else item["fileOffset"],
                                    -1 if item["virtualAddress"] is None else item["virtualAddress"],
                                    item["evidenceHash"]))


def _finish(base: dict[str, Any], findings: list[dict[str, Any]], stages: list[dict[str, Any]]) -> dict[str, Any]:
    _sort_findings(findings)
    effects = {item["publicationEffect"] for item in findings}
    verdict = "rejected" if "block" in effects else "inconclusive" if "inconclusive" in effects else "accepted"
    base.update({"verdict": verdict, "publicationAllowed": verdict == "accepted",
                 "stages": stages, "findings": findings[:MAX_FINDINGS],
                 "notice": "Static analysis is not a sandbox or proof of safety; accepted EFI code remains trusted native firmware code."})
    encoded = json.dumps(base, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    base["reportDigest"] = hashlib.sha256(encoded).hexdigest()
    return base


def analyze(blob: bytes, trusted_keys: dict[bytes, Any] | None = None,
            *, deadline_seconds: float = DEFAULT_DEADLINE_SECONDS) -> dict[str, Any]:
    base: dict[str, Any] = {"schema": SCHEMA, "analyzerVersion": ANALYZER_VERSION,
                            "ruleSetVersion": RULESET_VERSION,
                            "bundleSha256": hashlib.sha256(blob).hexdigest(),
                            "bundleSize": len(blob), "bundleId": None,
                            "publisherKeyId": None, "gameId": None, "gameVersion": None}
    findings: list[dict[str, Any]] = []
    stages: list[dict[str, Any]] = []
    deadline = time.monotonic() + max(0.0, deadline_seconds)
    try:
        bundle = ocg2.decode(blob)
        stages.append({"name": "container", "status": "passed"})
    except ocg2.Ocg2Error as exc:
        findings.append(finding("OCSA-CONTAINER-INVALID", "critical", "verified", "block",
                                f"OCG2 validation failed with {exc.code}.",
                                "Rebuild the bundle with the matching OpenConsole pack tool.",
                                file_offset=exc.offset, evidence=blob[(exc.offset or 0):(exc.offset or 0) + 32]))
        stages.append({"name": "container", "status": "failed", "code": exc.code})
        return _finish(base, findings, stages)
    base.update({"bundleId": bundle.bundle_id.hex(),
                 "publisherKeyId": bundle.publisher_key_id.hex(),
                 "gameId": bundle.game_id, "gameVersion": bundle.version})
    key = (trusted_keys or {}).get(bundle.publisher_key_id)
    if key is None:
        findings.append(finding("OCSA-SIGNATURE-UNTRUSTED", "high", "verified", "inconclusive",
                                "The bundle is signed, but its publisher key is absent from the trust snapshot.",
                                "Analyze with an explicit trusted publisher key or complete publisher review."))
        stages.append({"name": "signature", "status": "untrusted"})
    else:
        try:
            ocg2.decode(blob, key)
            stages.append({"name": "signature", "status": "passed"})
        except ocg2.Ocg2Error as exc:
            findings.append(finding("OCSA-SIGNATURE-INVALID", "critical", "verified", "block",
                                    "The publisher signature did not verify.",
                                    "Sign the canonical bundle with the matching Ed448 private key."))
            stages.append({"name": "signature", "status": "failed", "code": exc.code})
    try:
        manifest_entry = next(entry for entry in bundle.entries if entry.role == ocg2.ROLES["manifest"])
        manifest = ocg2.parse_manifest(manifest_entry.data)
        base.update({"title": manifest["title"],
                     "developer": manifest.get("developer", "Unknown developer"),
                     "description": manifest.get("description", ""),
                     "compatibility": sorted(set(manifest["required_capabilities"]))})
        for entry in bundle.entries:
            if entry.role == ocg2.ROLES["jacket-ocj1"]:
                game_assets.validate_ocj1(entry.data)
            elif entry.role == ocg2.ROLES["controls-v1"]:
                game_assets.validate_controls_v1(entry.data)
        stages.append({"name": "manifest-assets", "status": "passed"})
    except (ocg2.Ocg2Error, game_assets.AssetError, StopIteration) as exc:
        code = getattr(exc, "code", "OCSA_MANIFEST_MISSING")
        findings.append(finding("OCSA-ASSET-INVALID", "high", "verified", "block",
                                f"Manifest or asset validation failed with {code}.",
                                "Correct the named package asset and rebuild the bundle."))
        stages.append({"name": "manifest-assets", "status": "failed", "code": code})
        return _finish(base, findings, stages)
    projected_prefix = f"\\EFI\\OpenConsole\\State\\Games\\{bundle.game_id}\\slot-a\\package\\"
    invalid_installed = [entry.path for entry in bundle.entries
                         if len(projected_prefix) + len(entry.path) >= 192]
    extracted_bytes = sum(len(entry.data) for entry in bundle.entries)
    if invalid_installed or extracted_bytes > ocg2.MAX_BUNDLE:
        findings.append(finding("OCSA-INSTALLED-TREE-INVALID", "high", "verified", "block",
                                "The authenticated package cannot be projected into the bounded installed-game tree.",
                                "Shorten package paths and keep extracted content within the documented installation limits.",
                                file=invalid_installed[0] if invalid_installed else "<bundle>"))
        stages.append({"name": "installed-tree", "status": "failed",
                       "code": "INSTALL_PATH_LIMIT" if invalid_installed else "INSTALL_SIZE_LIMIT"})
        return _finish(base, findings, stages)
    stages.append({"name": "installed-tree", "status": "passed",
                   "projectedFiles": len(bundle.entries), "projectedBytes": extracted_bytes})
    unsupported_required = sorted(set(manifest["required_capabilities"]) - SUPPORTED_CAPABILITIES)
    unsupported_optional = sorted(set(manifest["optional_capabilities"]) - SUPPORTED_CAPABILITIES)
    for capability in unsupported_required:
        findings.append(finding("OCSA-CAPABILITY-REQUIRED-UNSUPPORTED", "high", "verified", "block",
                                f"Required capability {capability} is outside this platform snapshot.",
                                "Target a supported version or publish with a matching platform profile.",
                                file=manifest_entry.path, evidence=capability.encode("ascii")))
    for capability in unsupported_optional:
        findings.append(finding("OCSA-CAPABILITY-OPTIONAL-UNSUPPORTED", "info", "verified", "none",
                                f"Optional capability {capability} is outside this platform snapshot.",
                                "Provide a graceful fallback when the capability is absent.",
                                file=manifest_entry.path, evidence=capability.encode("ascii")))
    stages.append({"name": "compatibility", "status": "failed" if unsupported_required else "passed",
                   "unsupportedRequired": unsupported_required,
                   "unsupportedOptional": unsupported_optional})
    architecture = str(manifest["architecture"])
    entry = next(item for item in bundle.entries
                 if item.role == ocg2.ROLES[ocg2.ENTRY_ROLE[architecture]])
    try:
        pe_info, sections = parse_pe(entry.data, architecture)
        stages.append({"name": "pe-coff", "status": "passed", "facts": pe_info})
    except (ValueError, struct.error) as exc:
        code = str(exc) if str(exc) else "PE_TRUNCATED"
        findings.append(finding("OCSA-PE-INVALID", "critical", "verified", "block",
                                f"The {architecture} EFI application failed PE/COFF validation: {code}.",
                                f"Link a bounded PE32+ {architecture} EFI application with subsystem 10 and a valid executable entry point.",
                                file=entry.path, evidence=entry.data[:32]))
        stages.append({"name": "pe-coff", "status": "failed", "code": code})
        return _finish(base, findings, stages)
    try:
        capabilities = set(manifest["required_capabilities"])
        findings.extend(scan_executable(entry.path, entry.data, sections, capabilities, deadline,
                                        architecture))
        stages.append({"name": "machine-code-indicators", "status": "passed",
                       "architecture": architecture,
                       "coverage": "capstone-x86-64" if architecture == "x64" else
                                   "capstone-arm64-system-instructions"})
    except AnalysisLimit as exc:
        findings.append(finding("OCSA-ANALYSIS-BUDGET", "high", "verified", "inconclusive",
                                str(exc), "Reduce executable size/complexity or request manual review.", file=entry.path))
        stages.append({"name": "machine-code-indicators", "status": "limit"})
    return _finish(base, findings, stages)


def to_text(report: dict[str, Any]) -> str:
    lines = [f"OpenConsole analyzer {report['analyzerVersion']} / rules {report['ruleSetVersion']}",
             f"Verdict: {report['verdict']} (publication allowed: {str(report['publicationAllowed']).lower()})",
             f"Bundle: {report['bundleSha256']} ({report['bundleSize']} bytes)"]
    for item in report["findings"]:
        location = item["file"] + (f":0x{item['fileOffset']:x}" if item["fileOffset"] is not None else "")
        lines.append(f"{item['severity'].upper()} {item['ruleId']} {location}: {item['explanation']}")
        lines.append(f"  Remediation: {item['remediation']}")
    lines.extend((f"Report digest: {report['reportDigest']}", report["notice"]))
    return "\n".join(lines) + "\n"


def to_sarif(report: dict[str, Any]) -> dict[str, Any]:
    levels = {"info": "note", "low": "note", "medium": "warning", "high": "error", "critical": "error"}
    results = []
    for item in report["findings"]:
        result: dict[str, Any] = {"ruleId": item["ruleId"], "level": levels[item["severity"]],
                                  "message": {"text": item["explanation"]},
                                  "properties": {"confidence": item["confidence"],
                                                 "publicationEffect": item["publicationEffect"],
                                                 "evidenceHash": item["evidenceHash"]}}
        if item["file"] != "<bundle>":
            region: dict[str, int] = {"byteOffset": item["fileOffset"] or 0}
            result["locations"] = [{"physicalLocation": {"artifactLocation": {"uri": item["file"]},
                                                            "region": region}}]
        results.append(result)
    return {"version": "2.1.0", "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
            "runs": [{"tool": {"driver": {"name": "OpenConsole Analyzer",
                                             "version": report["analyzerVersion"]}},
                      "results": results,
                      "properties": {"verdict": report["verdict"],
                                     "reportDigest": report["reportDigest"]}}]}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=pathlib.Path)
    parser.add_argument("--public-key", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--format", choices=("json", "text", "sarif"), default="json")
    args = parser.parse_args()
    try:
        keys = {}
        for path in args.public_key:
            key = ocg2.load_public_key(path)
            keys[hashlib.sha256(ocg2.public_bytes(key)).digest()] = key
        report = analyze(args.bundle.read_bytes(), keys)
        if args.format == "text":
            sys.stdout.write(to_text(report))
        else:
            output = to_sarif(report) if args.format == "sarif" else report
            print(json.dumps(output, sort_keys=True, indent=2))
        return 0 if report["verdict"] == "accepted" else 2
    except (OSError, ocg2.Ocg2Error) as exc:
        print(json.dumps({"code": getattr(exc, "code", "OCSA_IO"), "message": str(exc)}, sort_keys=True), file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
