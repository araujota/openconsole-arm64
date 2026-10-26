#!/usr/bin/env python3
import argparse, hashlib, json, pathlib, sys

def load(root): return json.loads((root / "source-inventory.json").read_text())
def main():
    p=argparse.ArgumentParser(); p.add_argument("left",type=pathlib.Path); p.add_argument("right",type=pathlib.Path); a=p.parse_args()
    li,ri=a.left/"openconsole-development.fat",a.right/"openconsole-development.fat"
    lh,rh=hashlib.sha256(li.read_bytes()).hexdigest(),hashlib.sha256(ri.read_bytes()).hexdigest()
    if lh==rh: print(json.dumps({"equal":True,"sha256":lh},sort_keys=True)); return 0
    left={x["path"]:x for x in load(a.left)["files"]}; right={x["path"]:x for x in load(a.right)["files"]}
    for path in sorted(set(left)|set(right)):
        if left.get(path)!=right.get(path):
            print(json.dumps({"equal":False,"firstInventoryDifference":path,"left":left.get(path),"right":right.get(path),"leftImageSha256":lh,"rightImageSha256":rh},sort_keys=True)); return 1
    lb,rb=li.read_bytes(),ri.read_bytes(); at=next((i for i,(x,y) in enumerate(zip(lb,rb)) if x!=y),min(len(lb),len(rb)))
    print(json.dumps({"equal":False,"firstImageByteDifference":at,"leftImageSha256":lh,"rightImageSha256":rh},sort_keys=True)); return 1
if __name__=="__main__": sys.exit(main())
