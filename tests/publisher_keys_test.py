#!/usr/bin/env python3
import importlib.util,pathlib,sys,tempfile
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PrivateKey
ROOT=pathlib.Path(__file__).resolve().parents[1];spec=importlib.util.spec_from_file_location("publisher_keys",ROOT/"tools/publisher_keys.py");assert spec and spec.loader;m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def reject(code,call):
 try:call()
 except m.Error as e:assert e.code==code,(e.code,code)
 else:raise AssertionError(code)
with tempfile.TemporaryDirectory() as td:
 root=pathlib.Path(td);key=Ed448PrivateKey.from_private_bytes(bytes(range(1,58))).public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw);(root/"key.pub").write_bytes(key)
 a=m.build([("openconsole","OpenConsole examples",root/"key.pub",1)]);b=m.build([("openconsole","OpenConsole examples",root/"key.pub",1)]);assert a==b
 records=m.decode(a);assert records[0]["state"]=="active" and records[0]["publisherId"]=="openconsole"
 bad=bytearray(a);bad[-1]^=1;reject("OCPK_DIGEST",lambda:m.decode(bytes(bad)))
 (ROOT/"build/publisher-keys-fixture.ocpk").write_bytes(a);(ROOT/"build/publisher-keys-fixture.pub").write_bytes(key)
print("PASS: canonical publisher trust store, deterministic encoding, and tamper rejection")
