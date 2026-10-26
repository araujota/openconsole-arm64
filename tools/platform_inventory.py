#!/usr/bin/env python3
"""Build and verify deterministic OpenConsole OCP1 platform inventories."""

from __future__ import annotations

import argparse, hashlib, json, pathlib, re, struct, sys
from dataclasses import dataclass
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey, Ed448PublicKey

ENVELOPE = struct.Struct("<4sHHIIIHHQ32s32s32s")
INDEX_HEADER = struct.Struct("<4sHHIIIIIQ28s")
ENTRY = struct.Struct("<HHIQ32sHH")
DOMAIN = b"OPENCONSOLE-PLATFORM-SIGNED\0"
MAX_INDEX, MAX_ENTRIES, MAX_PATH, MAX_FILE = 256 * 1024, 256, 240, 64 * 1024 * 1024
ROLES = {"parent": 1, "input": 10, "network": 11, "audio": 12, "crypto": 13,
         "graphics": 14, "trust": 20, "notices": 30, "recovery": 40}
ROLE_NAMES = {value: key for key, value in ROLES.items()}
PATH_RE = re.compile(r"^[A-Za-z0-9._/-]+$")
VERSION_RE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z.+_-]{0,31}$")


class InventoryError(ValueError):
    def __init__(self, code, message): super().__init__(message); self.code = code


@dataclass(frozen=True)
class Record:
    path: str
    role: int
    baseline: bool
    size: int
    digest: bytes
    version: str
    key_scope: int


@dataclass(frozen=True)
class Inventory:
    inventory_id: bytes
    key_id: bytes
    profile: str
    parent_version: str
    allow_unverified_local: bool
    records: tuple[Record, ...]
    signature_verified: bool


def path_valid(path: str) -> str:
    try: raw = path.encode("ascii")
    except UnicodeEncodeError as exc: raise InventoryError("OCP1_BAD_PATH", "path is not ASCII") from exc
    if not 1 <= len(raw) <= MAX_PATH or not PATH_RE.fullmatch(path) or path.startswith("/"):
        raise InventoryError("OCP1_BAD_PATH", f"invalid platform path: {path!r}")
    if any(part in ("", ".", "..") for part in path.split("/")):
        raise InventoryError("OCP1_BAD_PATH", f"invalid path component: {path!r}")
    folded = path.lower()
    if not folded.startswith(("efi/boot/", "efi/openconsole/")) or folded == "efi/openconsole/platform.ocp":
        raise InventoryError("OCP1_BAD_PATH", f"path is outside inventory authority: {path!r}")
    return path


def key_bytes(key):
    return key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def private_key(path):
    raw = path.read_bytes()
    key = Ed448PrivateKey.from_private_bytes(raw) if len(raw) == 57 else serialization.load_pem_private_key(raw, None)
    if not isinstance(key, Ed448PrivateKey): raise InventoryError("OCP1_BAD_SIGNATURE", "not an Ed448 private key")
    return key


def public_key(path):
    raw = path.read_bytes()
    key = Ed448PublicKey.from_public_bytes(raw) if len(raw) == 57 else serialization.load_pem_public_key(raw)
    if not isinstance(key, Ed448PublicKey): raise InventoryError("OCP1_UNTRUSTED_KEY", "not an Ed448 public key")
    return key


def canonical_index(profile: str, parent_version: str, allow_unverified: bool,
                    records: list[Record]) -> bytes:
    if profile not in ("development", "test", "release") or not VERSION_RE.fullmatch(parent_version):
        raise InventoryError("OCP1_BAD_INDEX", "invalid profile or parent version")
    if not 1 <= len(records) <= MAX_ENTRIES: raise InventoryError("OCP1_LIMIT", "invalid entry count")
    ordered = sorted(records, key=lambda r: r.path.encode("ascii"))
    seen, encoded = set(), bytearray()
    for record in ordered:
        path, version = path_valid(record.path), record.version
        if path.lower() in seen: raise InventoryError("OCP1_PATH_COLLISION", f"collision: {path}")
        seen.add(path.lower())
        if record.role not in ROLE_NAMES or record.key_scope not in (1, 2) or record.size > MAX_FILE:
            raise InventoryError("OCP1_BAD_ROLE", f"invalid role/scope/size: {path}")
        if not VERSION_RE.fullmatch(version): raise InventoryError("OCP1_BAD_INDEX", f"invalid version: {path}")
        pb, vb = path.encode("ascii"), version.encode("ascii")
        encoded += ENTRY.pack(len(pb), record.role, int(record.baseline), record.size,
                              record.digest, len(vb), record.key_scope) + pb + vb
    profile_b, version_b = profile.encode("ascii"), parent_version.encode("ascii")
    index = INDEX_HEADER.pack(b"OPI1", 1, 0, 64, len(records), len(profile_b), len(version_b),
                              int(allow_unverified), len(encoded), bytes(28))
    index += profile_b + version_b + encoded
    if len(index) > MAX_INDEX: raise InventoryError("OCP1_LIMIT", "inventory index exceeds 256 KiB")
    return index


def build(root: pathlib.Path, profile: str, parent_version: str, allow_unverified: bool,
          specs: list[tuple[str, str, str, int, bool]], key: Ed448PrivateKey) -> bytes:
    records = []
    for role_name, path, version, scope, baseline in specs:
        data = (root / path).read_bytes()
        records.append(Record(path, ROLES[role_name], baseline, len(data), hashlib.sha256(data).digest(), version, scope))
    index = canonical_index(profile, parent_version, allow_unverified, records)
    inventory_id, key_id = hashlib.sha256(index).digest(), hashlib.sha256(key_bytes(key.public_key())).digest()
    total = ENVELOPE.size + 114 + len(index)
    header = ENVELOPE.pack(b"OCP1", 1, 0, 128, len(index), len(records), 1, 114, total,
                           inventory_id, key_id, bytes(32))
    return header + key.sign(DOMAIN + header + index) + index


def decode(blob: bytes, key: Ed448PublicKey | None = None, root: pathlib.Path | None = None) -> Inventory:
    if len(blob) < 128: raise InventoryError("OCP1_LIMIT", "inventory is truncated")
    magic, major, minor, hs, ilen, count, alg, slen, total, iid, kid, reserved = ENVELOPE.unpack_from(blob)
    if magic != b"OCP1": raise InventoryError("OCP1_BAD_MAGIC", "not OCP1")
    if (major, minor) != (1, 0): raise InventoryError("OCP1_UNSUPPORTED_VERSION", "unsupported OCP1 version")
    if hs != 128 or ilen > MAX_INDEX or count > MAX_ENTRIES or alg != 1 or slen != 114 or any(reserved):
        raise InventoryError("OCP1_BAD_HEADER", "noncanonical inventory header")
    if total != len(blob) or total != hs + slen + ilen: raise InventoryError("OCP1_LENGTH_MISMATCH", "length mismatch")
    signature, index = blob[hs:hs+slen], blob[hs+slen:]
    if hashlib.sha256(index).digest() != iid: raise InventoryError("OCP1_INDEX_DIGEST", "index digest mismatch")
    verified = False
    if key:
        if hashlib.sha256(key_bytes(key)).digest() != kid: raise InventoryError("OCP1_UNTRUSTED_KEY", "key ID mismatch")
        try: key.verify(signature, DOMAIN + blob[:hs] + index)
        except Exception as exc: raise InventoryError("OCP1_BAD_SIGNATURE", "signature failed") from exc
        verified = True
    if len(index) < 64: raise InventoryError("OCP1_BAD_INDEX", "truncated index")
    im, imaj, imin, ihs, icount, plen, vlen, flags, rlen, izero = INDEX_HEADER.unpack_from(index)
    if (im, imaj, imin, ihs) != (b"OPI1", 1, 0, 64) or icount != count or flags & ~1 or any(izero):
        raise InventoryError("OCP1_BAD_INDEX", "noncanonical index header")
    start, records_start = 64, 64 + plen + vlen
    if records_start + rlen != len(index): raise InventoryError("OCP1_BAD_INDEX", "record length mismatch")
    try: profile = index[start:start+plen].decode("ascii"); parent_version = index[start+plen:records_start].decode("ascii")
    except UnicodeDecodeError as exc: raise InventoryError("OCP1_BAD_INDEX", "non-ASCII index") from exc
    cursor, prior, seen, records = records_start, b"", set(), []
    for _ in range(count):
        if cursor + ENTRY.size > len(index): raise InventoryError("OCP1_BAD_INDEX", "truncated record")
        path_len, role, eflags, size, digest, ver_len, scope = ENTRY.unpack_from(index, cursor); cursor += ENTRY.size
        if cursor + path_len + ver_len > len(index): raise InventoryError("OCP1_BAD_INDEX", "truncated record strings")
        pb, vb = index[cursor:cursor+path_len], index[cursor+path_len:cursor+path_len+ver_len]; cursor += path_len + ver_len
        try: path, version = path_valid(pb.decode("ascii")), vb.decode("ascii")
        except (UnicodeDecodeError, InventoryError) as exc: raise InventoryError("OCP1_BAD_PATH", "invalid record path") from exc
        if pb <= prior and prior or path.lower() in seen: raise InventoryError("OCP1_BAD_ORDER", "unordered or colliding paths")
        if role not in ROLE_NAMES or eflags & ~1 or scope not in (1, 2): raise InventoryError("OCP1_BAD_ROLE", "invalid record role")
        record = Record(path, role, bool(eflags), size, digest, version, scope)
        if root is not None:
            try: data = (root / path).read_bytes()
            except OSError as exc: raise InventoryError("OCP1_FILE_MISSING", f"missing inventoried file: {path}") from exc
            if len(data) != size or hashlib.sha256(data).digest() != digest:
                raise InventoryError("OCP1_FILE_DIGEST", f"inventoried file mismatch: {path}")
        records.append(record); prior = pb; seen.add(path.lower())
    if cursor != len(index): raise InventoryError("OCP1_BAD_INDEX", "trailing index bytes")
    return Inventory(iid, kid, profile, parent_version, bool(flags & 1), tuple(records), verified)


def spec_arg(value):
    parts = value.split(":", 4)
    if len(parts) != 5 or parts[0] not in ROLES: raise argparse.ArgumentTypeError("ROLE:PATH:VERSION:SCOPE:BASELINE")
    return parts[0], parts[1], parts[2], int(parts[3]), parts[4] == "1"


def result(inv):
    return {"schema":"openconsole.ocp1.inspect.v1", "inventoryId":inv.inventory_id.hex(),
            "keyId":inv.key_id.hex(), "profile":inv.profile, "parentVersion":inv.parent_version,
            "allowUnverifiedLocal":inv.allow_unverified_local, "signatureVerified":inv.signature_verified,
            "files":[{"path":r.path,"role":ROLE_NAMES[r.role],"baseline":r.baseline,"size":r.size,
                      "sha256":r.digest.hex(),"version":r.version,"keyScope":r.key_scope} for r in inv.records]}


def main():
    p=argparse.ArgumentParser(); sub=p.add_subparsers(dest="cmd",required=True)
    b=sub.add_parser("build"); b.add_argument("root",type=pathlib.Path); b.add_argument("output",type=pathlib.Path)
    b.add_argument("--profile",required=True); b.add_argument("--parent-version",required=True); b.add_argument("--private-key",required=True,type=pathlib.Path)
    b.add_argument("--allow-unverified-local",action="store_true"); b.add_argument("--entry",action="append",type=spec_arg,default=[])
    for name in ("inspect","verify"):
        q=sub.add_parser(name); q.add_argument("inventory",type=pathlib.Path); q.add_argument("--public-key",type=pathlib.Path); q.add_argument("--root",type=pathlib.Path)
    a=p.parse_args()
    try:
        if a.cmd=="build":
            key=private_key(a.private_key); blob=build(a.root,a.profile,a.parent_version,a.allow_unverified_local,a.entry,key)
            a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_bytes(blob); inv=decode(blob,key.public_key(),a.root)
        else:
            key=public_key(a.public_key) if a.public_key else None
            if a.cmd=="verify" and key is None: raise InventoryError("OCP1_UNTRUSTED_KEY","verify requires --public-key")
            inv=decode(a.inventory.read_bytes(),key,a.root)
        print(json.dumps(result(inv),sort_keys=True,indent=2)); return 0
    except (InventoryError,OSError,ValueError) as exc:
        print(json.dumps({"code":getattr(exc,"code","OCP1_IO"),"message":str(exc)},sort_keys=True),file=sys.stderr); return 2


if __name__=="__main__": raise SystemExit(main())
