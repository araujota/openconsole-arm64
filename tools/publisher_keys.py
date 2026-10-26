#!/usr/bin/env python3
"""Build and inspect canonical OCPK publisher trust stores."""
import argparse,hashlib,json,pathlib,re,struct,sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed448 import Ed448PublicKey
HEADER=struct.Struct("<4sHHIIIIQ32s"); PREFIX=struct.Struct("<32s57sIBBB8s")
MAX_RECORDS=256;MAX_BYTES=65536;ID=re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}$")
class Error(ValueError):
 def __init__(self,code,message):super().__init__(message);self.code=code
def public(path):
 raw=path.read_bytes();key=Ed448PublicKey.from_public_bytes(raw) if len(raw)==57 else serialization.load_pem_public_key(raw)
 if not isinstance(key,Ed448PublicKey):raise Error("OCPK_BAD_KEY","not Ed448")
 return key.public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
def build(entries):
 records=[]
 for publisher,name,path,state in entries:
  if not ID.fullmatch(publisher) or not 1<=len(name.encode("ascii"))<=64 or state not in (1,2):raise Error("OCPK_BAD_RECORD","invalid publisher record")
  key=public(path);kid=hashlib.sha256(key).digest();pb=publisher.encode();nb=name.encode();records.append((kid,PREFIX.pack(kid,key,1,state,len(pb),len(nb),bytes(8))+pb+nb))
 records.sort();region=b"".join(x[1] for x in records)
 if not 0<=len(records)<=MAX_RECORDS or len(region)>MAX_BYTES or len({x[0] for x in records})!=len(records):raise Error("OCPK_LIMIT","key store limit/collision")
 return HEADER.pack(b"OCPK",1,0,64,len(records),len(region),0,64+len(region),hashlib.sha256(region).digest())+region
def decode(blob):
 if len(blob)<64:raise Error("OCPK_TRUNCATED","truncated header")
 magic,major,minor,hs,count,rlen,flags,total,digest=HEADER.unpack_from(blob)
 if magic!=b"OCPK":raise Error("OCPK_BAD_MAGIC","not OCPK")
 if (major,minor)!=(1,0):raise Error("OCPK_UNSUPPORTED_VERSION","unsupported version")
 if hs!=64 or count>MAX_RECORDS or rlen>MAX_BYTES or flags or total!=len(blob) or total!=64+rlen:raise Error("OCPK_BAD_HEADER","noncanonical header")
 region=blob[64:]
 if hashlib.sha256(region).digest()!=digest:raise Error("OCPK_DIGEST","record digest mismatch")
 at=0;prior=b"";result=[]
 for _ in range(count):
  if at+104>len(region):raise Error("OCPK_TRUNCATED","truncated record")
  kid,key,scopes,state,il,nl,reserved=PREFIX.unpack_from(region,at);at+=104
  if at+il+nl>len(region):raise Error("OCPK_TRUNCATED","truncated strings")
  try:publisher=region[at:at+il].decode("ascii");name=region[at+il:at+il+nl].decode("ascii")
  except UnicodeDecodeError as e:raise Error("OCPK_BAD_RECORD","non-ASCII strings") from e
  at+=il+nl
  if kid<=prior or hashlib.sha256(key).digest()!=kid or scopes!=1 or state not in (1,2) or any(reserved) or not ID.fullmatch(publisher) or not 1<=nl<=64 or any(ord(c)<32 or ord(c)>126 for c in name):raise Error("OCPK_BAD_RECORD","noncanonical record")
  result.append({"keyId":kid.hex(),"publicKey":key.hex(),"publisherId":publisher,"displayName":name,"state":"active" if state==1 else "revoked","scopes":["game-signing"]});prior=kid
 if at!=len(region):raise Error("OCPK_BAD_HEADER","trailing bytes")
 return result
def entry(value):
 p=value.split(":",3)
 if len(p)!=4 or p[2] not in ("active","revoked"):raise argparse.ArgumentTypeError("ID:NAME:active|revoked:PUBLIC_KEY")
 return p[0],p[1],pathlib.Path(p[3]),1 if p[2]=="active" else 2
def main():
 p=argparse.ArgumentParser();s=p.add_subparsers(dest="cmd",required=True);b=s.add_parser("build");b.add_argument("output",type=pathlib.Path);b.add_argument("--key",action="append",type=entry,default=[]);i=s.add_parser("inspect");i.add_argument("store",type=pathlib.Path);a=p.parse_args()
 try:
  if a.cmd=="build":blob=build(a.key);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(blob)
  else:blob=a.store.read_bytes()
  print(json.dumps({"schema":"openconsole.ocpk.inspect.v1","sha256":hashlib.sha256(blob).hexdigest(),"publishers":decode(blob)},sort_keys=True,indent=2));return 0
 except (Error,OSError,ValueError) as e:print(json.dumps({"code":getattr(e,"code","OCPK_IO"),"message":str(e)},sort_keys=True),file=sys.stderr);return 2
if __name__=="__main__":raise SystemExit(main())
