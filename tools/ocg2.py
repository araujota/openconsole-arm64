#!/usr/bin/env python3
"""Deterministic OpenConsole OCG2 pack/inspect/verify tool."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
import sys
import tomllib
from dataclasses import dataclass

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey, Ed448PublicKey

MAX_BUNDLE = 64 * 1024 * 1024
MAX_INDEX = 256 * 1024
MAX_FILES = 256
MAX_PATH = 240
MAX_FILE = 32 * 1024 * 1024
SIGN_DOMAIN = b"OPENCONSOLE-OCG2-SIGNED\0"
ENVELOPE = struct.Struct("<4sHHIIQQIHHQ32s32s16s")
INDEX_HEADER = struct.Struct("<4sHHIIIIIIQQ32s")
ENTRY = struct.Struct("<HHIQQ32s")
ROLES = {"manifest": 1, "entry-x64": 2, "jacket-ocj1": 3, "controls-v1": 4,
         "data": 5, "entry-arm64": 6}
ROLE_NAMES = {value: name for name, value in ROLES.items()}
ENTRY_ROLE = {"x64": "entry-x64", "arm64": "entry-arm64"}
PE_MACHINE = {"x64": 0x8664, "arm64": 0xAA64}
PATH_RE = re.compile(r"^[A-Za-z0-9._/-]+$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}$")
VERSION_RE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z.+_-]{0,31}$")
CAPABILITY_RE = re.compile(r"^[a-z][a-z0-9_.-]{0,63}@[0-9]+\.[0-9]+$")


class Ocg2Error(ValueError):
    def __init__(self, code: str, message: str, *, entry: int | None = None, offset: int | None = None):
        super().__init__(message)
        self.code, self.entry, self.offset = code, entry, offset

    def as_dict(self) -> dict[str, object]:
        result: dict[str, object] = {"code": self.code, "message": str(self)}
        if self.entry is not None:
            result["entry"] = self.entry
        if self.offset is not None:
            result["offset"] = self.offset
        return result


@dataclass(frozen=True)
class FileEntry:
    path: str
    role: int
    offset: int
    data: bytes
    digest: bytes


@dataclass(frozen=True)
class Bundle:
    bundle_id: bytes
    publisher_key_id: bytes
    game_id: str
    version: str
    entries: tuple[FileEntry, ...]
    signature: bytes
    index: bytes


def checked_add(*values: int, limit: int = MAX_BUNDLE) -> int:
    total = 0
    for value in values:
        if value < 0 or total > limit - value:
            raise Ocg2Error("OCG2_OVERFLOW", "bounded length addition overflowed")
        total += value
    return total


def canonical_path(value: str) -> str:
    try:
        raw = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise Ocg2Error("OCG2_BAD_PATH", "path is not ASCII") from exc
    if not 1 <= len(raw) <= MAX_PATH or not PATH_RE.fullmatch(value):
        raise Ocg2Error("OCG2_BAD_PATH", f"invalid path syntax: {value!r}")
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise Ocg2Error("OCG2_BAD_PATH", f"path contains a forbidden component: {value!r}")
    folded = value.lower()
    if folded.startswith(("efi/", "games/", ".openconsole/", "openconsole-")):
        raise Ocg2Error("OCG2_BAD_PATH", f"path enters a reserved namespace: {value!r}")
    return value


def parse_manifest(data: bytes) -> dict[str, object]:
    if len(data) > 64 * 1024:
        raise Ocg2Error("OCG2_LIMIT", "manifest exceeds 64 KiB")
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "manifest 2.0 must be ASCII") from exc
    fields: dict[str, object] = {}
    string_re = re.compile(r'^"((?:[^"\\]|\\["\\nrt])*)"$')
    array_re = re.compile(r'^\[(.*)\]$')
    for line_no, source_line in enumerate(text.splitlines(), 1):
        line = source_line.split("#", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise Ocg2Error("OCG2_MANIFEST_MISMATCH", f"manifest syntax at line {line_no}")
        key, raw = (part.strip() for part in line.split("=", 1))
        if key in fields or not re.fullmatch(r"[a-z_][a-z0-9_]*", key):
            raise Ocg2Error("OCG2_MANIFEST_MISMATCH", f"duplicate or invalid key at line {line_no}")
        match = string_re.fullmatch(raw)
        if match:
            fields[key] = bytes(match.group(1), "ascii").decode("unicode_escape")
            continue
        array = array_re.fullmatch(raw)
        if array:
            body = array.group(1).strip()
            values: list[str] = []
            if body:
                for item in body.split(","):
                    item_match = string_re.fullmatch(item.strip())
                    if not item_match:
                        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", f"bad array at line {line_no}")
                    values.append(bytes(item_match.group(1), "ascii").decode("unicode_escape"))
            fields[key] = values
            continue
        if raw in ("true", "false"):
            fields[key] = raw == "true"
        elif raw.isdigit():
            fields[key] = int(raw)
        else:
            raise Ocg2Error("OCG2_MANIFEST_MISMATCH", f"unsupported value at line {line_no}")
    required = {
        "format_version", "package_format", "id", "title", "version", "entry",
        "architecture", "required_capabilities", "optional_capabilities",
        "graphics_api", "accel", "min_graphics_abi",
    }
    missing = sorted(required - fields.keys())
    if missing:
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "missing keys: " + ", ".join(missing))
    if fields["format_version"] != "2.0" or fields["package_format"] != "ocg2":
        raise Ocg2Error("OCG2_UNSUPPORTED_VERSION", "manifest format is not 2.0/ocg2")
    if not isinstance(fields["id"], str) or not ID_RE.fullmatch(fields["id"]):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "invalid game id")
    if not isinstance(fields["version"], str) or not VERSION_RE.fullmatch(fields["version"]):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "invalid game version")
    if fields["architecture"] not in ENTRY_ROLE or fields["graphics_api"] not in ("gop", "gles2"):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "unsupported architecture or graphics API")
    if fields["accel"] not in ("forbidden", "optional", "required"):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "invalid acceleration policy")
    required_caps, optional_caps = fields["required_capabilities"], fields["optional_capabilities"]
    if (not isinstance(required_caps, list) or not isinstance(optional_caps, list) or
            len(required_caps) > 32 or len(optional_caps) > 32 or
            not all(isinstance(item, str) and CAPABILITY_RE.fullmatch(item)
                    for item in required_caps + optional_caps) or
            len(set(required_caps)) != len(required_caps) or
            len(set(optional_caps)) != len(optional_caps) or
            set(required_caps) & set(optional_caps)):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "invalid capability lists")
    if (not isinstance(fields["min_graphics_abi"], int) or
            not 0 <= fields["min_graphics_abi"] <= 0xFFFFFFFF):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "invalid graphics ABI")
    for key in ("entry", "jacket", "controls"):
        if key in fields:
            if not isinstance(fields[key], str):
                raise Ocg2Error("OCG2_MANIFEST_MISMATCH", f"{key} must be a string")
            canonical_path(fields[key])
    return fields


def validate_entry_pe(data: bytes, architecture: str) -> None:
    if len(data) < 64 or data[:2] != b"MZ":
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "entry is not a PE/COFF image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe > len(data) - 24 or data[pe:pe + 4] != b"PE\0\0":
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "entry has no bounded PE signature")
    machine = struct.unpack_from("<H", data, pe + 4)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if (machine != PE_MACHINE[architecture] or optional_size < 112 or
            optional > len(data) - optional_size or
            struct.unpack_from("<H", data, optional)[0] != 0x20B or
            struct.unpack_from("<H", data, optional + 68)[0] != 10):
        raise Ocg2Error(
            "OCG2_MANIFEST_MISMATCH",
            f"entry PE machine/subsystem does not match {architecture} EFI application")


def load_private_key(path: pathlib.Path) -> Ed448PrivateKey:
    raw = path.read_bytes()
    if len(raw) == 57:
        return Ed448PrivateKey.from_private_bytes(raw)
    try:
        key = serialization.load_pem_private_key(raw, password=None)
    except ValueError as exc:
        raise Ocg2Error("OCG2_BAD_SIGNATURE", "private key is not raw Ed448 or PEM") from exc
    if not isinstance(key, Ed448PrivateKey):
        raise Ocg2Error("OCG2_BAD_SIGNATURE", "private key is not Ed448")
    return key


def load_public_key(path: pathlib.Path) -> Ed448PublicKey:
    raw = path.read_bytes()
    if len(raw) == 57:
        return Ed448PublicKey.from_public_bytes(raw)
    try:
        key = serialization.load_pem_public_key(raw)
    except ValueError as exc:
        raise Ocg2Error("OCG2_UNTRUSTED_KEY", "public key is not raw Ed448 or PEM") from exc
    if not isinstance(key, Ed448PublicKey):
        raise Ocg2Error("OCG2_UNTRUSTED_KEY", "public key is not Ed448")
    return key


def public_bytes(key: Ed448PublicKey) -> bytes:
    return key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def make_index(game_id: str, version: str, key_id: bytes,
               files: list[tuple[str, int, bytes]]) -> tuple[bytes, bytes, tuple[FileEntry, ...]]:
    if not ID_RE.fullmatch(game_id) or not VERSION_RE.fullmatch(version):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "invalid game id or version")
    if not 1 <= len(files) <= MAX_FILES:
        raise Ocg2Error("OCG2_LIMIT", "file count is outside OCG2 limits")
    prepared: list[tuple[str, int, bytes]] = []
    seen: set[str] = set()
    for path, role, data in files:
        path = canonical_path(path)
        folded = path.lower()
        if folded in seen:
            raise Ocg2Error("OCG2_PATH_COLLISION", f"case-fold path collision: {path}")
        seen.add(folded)
        if role not in ROLE_NAMES or len(data) > MAX_FILE:
            raise Ocg2Error("OCG2_LIMIT", f"bad role or oversized file: {path}")
        if role != ROLES["data"] and not data:
            raise Ocg2Error("OCG2_BAD_ROLE", f"required-role file is empty: {path}")
        prepared.append((path, role, data))
    prepared.sort(key=lambda item: item[0].encode("ascii"))
    manifests = [i for i, item in enumerate(prepared) if item[1] == ROLES["manifest"]]
    entry_roles = {ROLES["entry-x64"], ROLES["entry-arm64"]}
    entries = [i for i, item in enumerate(prepared) if item[1] in entry_roles]
    if len(manifests) != 1 or len(entries) != 1 or prepared[manifests[0]][0] != "game.toml":
        raise Ocg2Error("OCG2_BAD_ROLE", "bundle needs one game.toml manifest and one architecture entry")
    if sum(item[1] == ROLES["jacket-ocj1"] for item in prepared) > 1 or sum(
            item[1] == ROLES["controls-v1"] for item in prepared) > 1:
        raise Ocg2Error("OCG2_BAD_ROLE", "jacket and controls roles are singular")
    offset, records, payload, built = 0, bytearray(), bytearray(), []
    for path, role, data in prepared:
        digest = hashlib.sha256(data).digest()
        path_bytes = path.encode("ascii")
        records += ENTRY.pack(len(path_bytes), role, 0, offset, len(data), digest) + path_bytes
        payload += data
        built.append(FileEntry(path, role, offset, data, digest))
        offset = checked_add(offset, len(data))
    game_bytes, version_bytes = game_id.encode("ascii"), version.encode("ascii")
    index = INDEX_HEADER.pack(b"OCI2", 2, 0, 80, len(built), manifests[0], len(game_bytes),
                              len(version_bytes), 0, len(records), len(payload), key_id)
    index += game_bytes + version_bytes + records
    if len(index) > MAX_INDEX:
        raise Ocg2Error("OCG2_LIMIT", "canonical index exceeds 256 KiB")
    return index, bytes(payload), tuple(built)


def pack(manifest_path: pathlib.Path, role_sources: dict[str, pathlib.Path],
         private_key: Ed448PrivateKey) -> bytes:
    manifest = manifest_path.read_bytes()
    fields = parse_manifest(manifest)
    files: list[tuple[str, int, bytes]] = [("game.toml", ROLES["manifest"], manifest)]
    architecture = str(fields["architecture"])
    entry_role = ENTRY_ROLE[architecture]
    expected_roles = (("entry", entry_role), ("jacket", "jacket-ocj1"),
                      ("controls", "controls-v1"))
    consumed: set[str] = set()
    for field, role_name in expected_roles:
        if field in fields:
            if role_name not in role_sources:
                raise Ocg2Error("OCG2_MANIFEST_MISMATCH", f"missing source for {role_name}")
            consumed.add(role_name)
            content = role_sources[role_name].read_bytes()
            if field == "entry":
                validate_entry_pe(content, architecture)
            files.append((str(fields[field]), ROLES[role_name], content))
    for key, source in role_sources.items():
        if key.startswith("data:"):
            consumed.add(key)
            files.append((key[5:], ROLES["data"], source.read_bytes()))
    unused = sorted(set(role_sources) - consumed)
    if unused:
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "unused or wrong-architecture roles: " + ", ".join(unused))
    pub_raw = public_bytes(private_key.public_key())
    key_id = hashlib.sha256(pub_raw).digest()
    index, payload, _ = make_index(str(fields["id"]), str(fields["version"]), key_id, files)
    bundle_id = hashlib.sha256(index).digest()
    total = checked_add(ENVELOPE.size, 114, len(index), len(payload))
    header = ENVELOPE.pack(b"OCG2", 2, 0, ENVELOPE.size, 0, len(index), len(payload),
                           len(files), 1, 114, total, bundle_id, key_id, bytes(16))
    signature = private_key.sign(SIGN_DOMAIN + header + index)
    return header + signature + index + payload


def decode(data: bytes, public_key: Ed448PublicKey | None = None,
           verify_payload: bool = True) -> Bundle:
    if len(data) > MAX_BUNDLE or len(data) < ENVELOPE.size:
        raise Ocg2Error("OCG2_LIMIT", "bundle size is outside OCG2 limits")
    fields = ENVELOPE.unpack_from(data)
    (magic, major, minor, header_size, flags, index_len, payload_len, file_count,
     sig_alg, sig_len, total, bundle_id, key_id, reserved) = fields
    if magic != b"OCG2":
        raise Ocg2Error("OCG2_BAD_MAGIC", "not an OCG2 bundle")
    if (major, minor) != (2, 0):
        raise Ocg2Error("OCG2_UNSUPPORTED_VERSION", f"unsupported OCG2 {major}.{minor}")
    if header_size != 128 or flags or sig_alg != 1 or sig_len != 114 or any(reserved):
        raise Ocg2Error("OCG2_BAD_HEADER", "noncanonical OCG2 header")
    if index_len > MAX_INDEX or file_count > MAX_FILES:
        raise Ocg2Error("OCG2_LIMIT", "index or file count exceeds limits")
    expected = checked_add(header_size, sig_len, index_len, payload_len)
    if total != expected or total != len(data):
        raise Ocg2Error("OCG2_LENGTH_MISMATCH", "declared bundle lengths do not match input")
    signature = data[header_size:header_size + sig_len]
    index_start = header_size + sig_len
    index = data[index_start:index_start + index_len]
    payload = data[index_start + index_len:]
    if hashlib.sha256(index).digest() != bundle_id:
        raise Ocg2Error("OCG2_INDEX_DIGEST", "canonical index digest mismatch")
    if public_key is not None:
        raw = public_bytes(public_key)
        if hashlib.sha256(raw).digest() != key_id:
            raise Ocg2Error("OCG2_UNTRUSTED_KEY", "public key ID does not match bundle")
        try:
            public_key.verify(signature, SIGN_DOMAIN + data[:header_size] + index)
        except Exception as exc:
            raise Ocg2Error("OCG2_BAD_SIGNATURE", "Ed448 signature verification failed") from exc
    if len(index) < INDEX_HEADER.size:
        raise Ocg2Error("OCG2_BAD_INDEX", "truncated index header")
    ih = INDEX_HEADER.unpack_from(index)
    (im, imaj, imin, ihs, count, manifest_i, gid_len, ver_len, iflags,
     records_len, indexed_payload_len, indexed_key_id) = ih
    if (im, imaj, imin, ihs, iflags) != (b"OCI2", 2, 0, 80, 0):
        raise Ocg2Error("OCG2_BAD_INDEX", "noncanonical index header")
    if count != file_count or indexed_payload_len != payload_len or indexed_key_id != key_id:
        raise Ocg2Error("OCG2_BAD_INDEX", "index and envelope disagree")
    strings_end = checked_add(ihs, gid_len, ver_len, limit=len(index))
    records_end = checked_add(strings_end, records_len, limit=len(index))
    if records_end != len(index):
        raise Ocg2Error("OCG2_BAD_INDEX", "index record region length mismatch")
    try:
        game_id = index[ihs:ihs + gid_len].decode("ascii")
        version = index[ihs + gid_len:strings_end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise Ocg2Error("OCG2_BAD_INDEX", "non-ASCII index identity") from exc
    if not ID_RE.fullmatch(game_id) or not VERSION_RE.fullmatch(version):
        raise Ocg2Error("OCG2_BAD_INDEX", "invalid index identity")
    cursor, previous_path, previous_end, folded = strings_end, b"", 0, set()
    entries: list[FileEntry] = []
    for entry_index in range(count):
        if cursor > len(index) - ENTRY.size:
            raise Ocg2Error("OCG2_BAD_INDEX", "truncated entry", entry=entry_index, offset=cursor)
        path_len, role, eflags, offset, length, digest = ENTRY.unpack_from(index, cursor)
        cursor += ENTRY.size
        if cursor > len(index) - path_len:
            raise Ocg2Error("OCG2_BAD_INDEX", "truncated path", entry=entry_index, offset=cursor)
        path_bytes = index[cursor:cursor + path_len]
        cursor += path_len
        try:
            path = canonical_path(path_bytes.decode("ascii"))
        except (UnicodeDecodeError, Ocg2Error) as exc:
            raise Ocg2Error("OCG2_BAD_PATH", "invalid indexed path", entry=entry_index) from exc
        if eflags or role not in ROLE_NAMES:
            raise Ocg2Error("OCG2_BAD_ROLE", "unknown entry flags or role", entry=entry_index)
        if path_bytes <= previous_path and entry_index:
            raise Ocg2Error("OCG2_BAD_ORDER", "paths are not strictly sorted", entry=entry_index)
        if path.lower() in folded:
            raise Ocg2Error("OCG2_PATH_COLLISION", "case-fold path collision", entry=entry_index)
        if offset != previous_end or length > MAX_FILE or offset > len(payload) - length:
            raise Ocg2Error("OCG2_BAD_OFFSET", "noncontiguous or out-of-range payload", entry=entry_index)
        file_data = payload[offset:offset + length]
        if verify_payload and hashlib.sha256(file_data).digest() != digest:
            raise Ocg2Error("OCG2_BAD_DIGEST", "file digest mismatch", entry=entry_index)
        entries.append(FileEntry(path, role, offset, file_data, digest))
        previous_path, previous_end = path_bytes, offset + length
        folded.add(path.lower())
    if cursor != len(index) or previous_end != len(payload) or manifest_i >= len(entries):
        raise Ocg2Error("OCG2_BAD_INDEX", "index coverage mismatch")
    manifests = [i for i, entry in enumerate(entries) if entry.role == ROLES["manifest"]]
    entry_images = [entry for entry in entries
                    if entry.role in (ROLES["entry-x64"], ROLES["entry-arm64"])]
    if manifests != [manifest_i] or entries[manifest_i].path != "game.toml" or len(entry_images) != 1:
        raise Ocg2Error("OCG2_BAD_ROLE", "manifest or entry role cardinality mismatch")
    manifest = parse_manifest(entries[manifest_i].data)
    if manifest["id"] != game_id or manifest["version"] != version:
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "manifest and index identity disagree")
    by_role = {entry.role: entry.path for entry in entries if entry.role != ROLES["data"]}
    architecture = str(manifest["architecture"])
    expected_entry_role = ROLES[ENTRY_ROLE[architecture]]
    if entry_images[0].role != expected_entry_role:
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "manifest architecture and entry role disagree")
    validate_entry_pe(entry_images[0].data, architecture)
    expected = (("entry", expected_entry_role), ("jacket", ROLES["jacket-ocj1"]),
                ("controls", ROLES["controls-v1"]))
    for field, role in expected:
        if (field in manifest) != (role in by_role) or field in manifest and manifest[field] != by_role[role]:
            raise Ocg2Error("OCG2_MANIFEST_MISMATCH", f"manifest {field} does not match role")
    return Bundle(bundle_id, key_id, game_id, version, tuple(entries), signature, index)


def report(bundle: Bundle, verified: bool) -> dict[str, object]:
    return {
        "schema": "openconsole.ocg2.inspect.v1",
        "bundleId": bundle.bundle_id.hex(),
        "publisherKeyId": bundle.publisher_key_id.hex(),
        "gameId": bundle.game_id,
        "version": bundle.version,
        "signatureVerified": verified,
        "files": [{"path": e.path, "role": ROLE_NAMES[e.role], "offset": e.offset,
                   "size": len(e.data), "sha256": e.digest.hex()} for e in bundle.entries],
    }


def role_arg(value: str) -> tuple[str, pathlib.Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("expected ROLE=FILE or data:PATH=FILE")
    role, source = value.split("=", 1)
    if role not in ("entry-x64", "entry-arm64", "jacket-ocj1", "controls-v1") and not role.startswith("data:"):
        raise argparse.ArgumentTypeError("unknown role")
    return role, pathlib.Path(source)


def migrate_v1(data: bytes, fallback_version: str) -> bytes:
    try:
        fields = tomllib.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as exc:
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "legacy manifest is not valid UTF-8 TOML") from exc
    allowed = {"id", "title", "entry", "architecture", "required_protocols",
               "graphics_api", "accel", "min_graphics_abi", "developer",
               "description", "version", "website"}
    if set(fields) - allowed:
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "legacy manifest has unknown keys")
    required = allowed - {"developer", "description", "version", "website"}
    if not required <= fields.keys():
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "legacy manifest is missing required keys")
    versions = {
        "openconsole.gles2": "openconsole.gles2@0.1",
        "openconsole.net": "openconsole.net@0.1",
        "openconsole.audio": "openconsole.audio@0.2",
        "input.keyboard": "uefi.input.keyboard@1.0",
        "input.pointer": "uefi.input.pointer@1.0",
    }
    protocols = fields["required_protocols"]
    if not isinstance(protocols, list) or not all(isinstance(item, str) for item in protocols):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "legacy required_protocols is invalid")
    capabilities: list[str] = []
    for protocol in protocols:
        if protocol not in versions:
            raise Ocg2Error("OCG2_MANIFEST_MISMATCH", f"no registered migration for {protocol}")
        capabilities.append(versions[protocol])
    if fields["graphics_api"] == "gles2" and "openconsole.gles2@0.1" not in capabilities:
        capabilities.append("openconsole.gles2@0.1")
    version = fields.get("version", fallback_version)
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "legacy version is invalid")
    def quoted(value: object) -> str:
        if not isinstance(value, str):
            raise Ocg2Error("OCG2_MANIFEST_MISMATCH", "legacy string field has wrong type")
        return json.dumps(value, ensure_ascii=True)
    lines = [
        'format_version = "2.0"',
        'package_format = "ocg2"',
        f'id = {quoted(fields["id"])}',
        f'title = {quoted(fields["title"])}',
        f'version = {quoted(version)}',
        f'entry = {quoted(fields["entry"])}',
        f'architecture = {quoted(fields["architecture"])}',
        "required_capabilities = [" + ", ".join(quoted(item) for item in capabilities) + "]",
        "optional_capabilities = []",
        f'graphics_api = {quoted(fields["graphics_api"])}',
        f'accel = {quoted("optional" if fields["accel"] else "forbidden")}',
        f'min_graphics_abi = {int(fields["min_graphics_abi"])}',
    ]
    for key in ("developer", "description", "website"):
        if key in fields:
            lines.append(f"{key} = {quoted(fields[key])}")
    migrated = ("\n".join(lines) + "\n").encode("ascii")
    parse_manifest(migrated)
    return migrated


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build, inspect, and verify deterministic signed OpenConsole game bundles.",
        epilog="See docs/building-an-ocg2-game.md for a complete developer workflow.")
    sub = parser.add_subparsers(dest="command", required=True)
    pack_parser = sub.add_parser(
        "pack", description="Canonicalize, sign, and write one OCG2 bundle.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""File mappings:
  --file entry-x64=HOST_FILE
  --file entry-arm64=HOST_FILE
  --file jacket-ocj1=HOST_FILE
  --file controls-v1=HOST_FILE
  --file data:PACKAGE/PATH=HOST_FILE

The destination for each singular role comes from game.toml. Data mappings
name their destination after `data:`. Repeat --file for additional data files.""")
    pack_parser.add_argument("manifest", type=pathlib.Path,
                             help="strict manifest-v2 game.toml")
    pack_parser.add_argument("output", type=pathlib.Path,
                             help="destination .ocg file")
    pack_parser.add_argument("--private-key", required=True, type=pathlib.Path,
                             help="raw 57-byte or PEM Ed448 private key")
    pack_parser.add_argument("--file", action="append", type=role_arg, default=[],
                             metavar="ROLE=FILE",
                             help="map a role or data:PACKAGE/PATH to a host file; repeatable")
    for name in ("inspect", "verify"):
        command = sub.add_parser(name, description=f"{name.title()} an OCG2 bundle.")
        command.add_argument("bundle", type=pathlib.Path, help="input .ocg file")
        command.add_argument("--public-key", type=pathlib.Path,
                             help="raw 57-byte or PEM Ed448 public key")
    unpack_parser = sub.add_parser("unpack", description="Verify and safely extract an OCG2 bundle.")
    unpack_parser.add_argument("bundle", type=pathlib.Path, help="input .ocg file")
    unpack_parser.add_argument("destination", type=pathlib.Path, help="new or empty output directory")
    unpack_parser.add_argument("--public-key", type=pathlib.Path,
                               help="optional publisher key used to verify the signature")
    migrate_parser = sub.add_parser("migrate-v1")
    migrate_parser.add_argument("manifest", type=pathlib.Path)
    migrate_parser.add_argument("output", type=pathlib.Path)
    migrate_parser.add_argument("--version", default="0.1.0")
    args = parser.parse_args()
    try:
        if args.command == "pack":
            role_sources = dict(args.file)
            blob = pack(args.manifest, role_sources, load_private_key(args.private_key))
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(blob)
            bundle = decode(blob, load_private_key(args.private_key).public_key())
            print(json.dumps(report(bundle, True), sort_keys=True, indent=2))
        elif args.command in ("inspect", "verify"):
            key = load_public_key(args.public_key) if args.public_key else None
            if args.command == "verify" and key is None:
                raise Ocg2Error("OCG2_UNTRUSTED_KEY", "verify requires --public-key")
            bundle = decode(args.bundle.read_bytes(), key)
            print(json.dumps(report(bundle, key is not None), sort_keys=True, indent=2))
        elif args.command == "unpack":
            key = load_public_key(args.public_key) if args.public_key else None
            bundle = decode(args.bundle.read_bytes(), key)
            args.destination.mkdir(parents=True, exist_ok=True)
            if any(args.destination.iterdir()):
                raise Ocg2Error("OCG2_IO", "unpack destination must be empty")
            for item in bundle.entries:
                target = args.destination.joinpath(*item.path.split("/"))
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(item.data)
            print(json.dumps({"schema": "openconsole.ocg2.unpack.v1",
                              "destination": str(args.destination),
                              "files": len(bundle.entries),
                              "signatureVerified": key is not None}, sort_keys=True))
        else:
            migrated = migrate_v1(args.manifest.read_bytes(), args.version)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(migrated)
            print(json.dumps({"schema": "openconsole.manifest-migration.v1",
                              "output": str(args.output), "bytes": len(migrated)}, sort_keys=True))
        return 0
    except (Ocg2Error, OSError) as exc:
        payload = exc.as_dict() if isinstance(exc, Ocg2Error) else {
            "code": "OCG2_IO", "message": str(exc)}
        print(json.dumps(payload, sort_keys=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
