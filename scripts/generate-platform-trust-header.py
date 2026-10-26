#!/usr/bin/env python3
"""Generate public, reproducible trust constants for a development parent."""
import argparse,hashlib,pathlib
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey

def array(name,data):
    rows=[]
    for at in range(0,len(data),12): rows.append("    "+",".join(f"0x{x:02x}u" for x in data[at:at+12]))
    return f"static const uint8_t {name}[{len(data)}] = {{\n"+",\n".join(rows)+"\n};\n"
def main():
    p=argparse.ArgumentParser();p.add_argument("--platform-key",type=pathlib.Path,required=True);p.add_argument("--crypto-driver",type=pathlib.Path,required=True);p.add_argument("--output",type=pathlib.Path,required=True);a=p.parse_args()
    raw=a.platform_key.read_bytes();key=Ed448PrivateKey.from_private_bytes(raw) if len(raw)==57 else serialization.load_pem_private_key(raw,None)
    if not isinstance(key,Ed448PrivateKey): raise SystemExit("platform key is not Ed448")
    public=key.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
    body="/* SPDX-License-Identifier: Apache-2.0 */\n#ifndef OPENCONSOLE_GENERATED_PLATFORM_TRUST_H\n#define OPENCONSOLE_GENERATED_PLATFORM_TRUST_H\n#include <stdint.h>\n"+array("oc_pinned_platform_public_key",public)+array("oc_pinned_crypto_driver_sha256",hashlib.sha256(a.crypto_driver.read_bytes()).digest())+"#endif\n"
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(body,encoding="ascii")
if __name__=="__main__":main()
