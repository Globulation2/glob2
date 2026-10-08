#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze an authored directory into a portable generator JSON package."""
import argparse
import json
from pathlib import Path

def pack(directory):
    root=Path(directory).resolve()
    modules={}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():raise ValueError('Package directories cannot contain symlinks')
        if path.is_file() and path.suffix=='.js':
            modules[path.relative_to(root).as_posix()]=path.read_text(encoding='utf-8')
    result=dict(formatVersion=1,manifest=json.loads((root/'manifest.json').read_text()),modules=modules)
    data=json.dumps(result,ensure_ascii=False,sort_keys=True,separators=(',',':'))+'\n'
    if len(data.encode())>4*1024*1024 or len(modules)>128:raise ValueError('Package exceeds runtime limits')
    if result['manifest'].get('entry','generator.js') not in modules:raise ValueError('Missing entry module')
    return data

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory');parser.add_argument('output')
    args=parser.parse_args();Path(args.output).write_text(pack(args.directory),encoding='utf-8')
if __name__=='__main__':main()
