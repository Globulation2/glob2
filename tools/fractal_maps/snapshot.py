#!/usr/bin/env python3
"""Retain dirty source and register an immutable tournament bundle using its public API."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from tools.tournaments.bundles import register_bundle
from tools.build_paths import native_binary


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('output');p.add_argument('--executable',default=str(native_binary()))
    args=p.parse_args();root=Path(args.output).resolve();root.mkdir(parents=True,exist_ok=False)
    paths=subprocess.check_output(['git','diff','--name-only'],text=True).splitlines()
    paths+=subprocess.check_output(['git','ls-files','--others','--exclude-standard'],text=True).splitlines()
    checksum=hashlib.sha256()
    for name in sorted(set(paths)):
        source=Path(name)
        if not source.is_file():continue
        data=source.read_bytes();checksum.update(name.encode()+b'\0'+data)
        target=root/'source'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    subprocess.run(['git','diff','--binary','--output='+str(root/'tracked.patch')],check=True)
    supplied=root/'supplied';supplied.mkdir();shutil.copy2(args.executable,supplied/'glob2')
    shutil.copytree('data',supplied/'data')
    revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
    bundle=register_bundle(supplied,root.parent/'bundles','glob2',revision,
                           dirty_identity=checksum.hexdigest(),options={'release':1,'server':0})
    identity={'bundle':bundle['id'],'source':checksum.hexdigest(),'revision':revision}
    (root/'identity.json').write_text(json.dumps(identity,indent=2)+'\n');print(json.dumps(identity))


if __name__=='__main__':main()
