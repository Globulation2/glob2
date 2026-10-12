#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the same 22 material fixtures captured by two actual render devices.

Native/desktop GL runner output is top-left BMP. The browser shader proof's raw
RGBA files are bottom-left 128x128. Differences are evidence; this tool does not
accept a platform or change renderer defaults.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image


def pixels(directory, identifier, browser):
    path=directory/(f'webgl-skin-{identifier}.rgba' if browser else f'material-{identifier}-body.bmp')
    if browser:
        raw=path.read_bytes()
        if len(raw)!=128*128*4: raise ValueError('Browser fixture must be 128x128 RGBA')
        data=b''.join(raw[y*512:(y+1)*512] for y in range(127,-1,-1))
    else:
        with Image.open(path) as image:
            if image.size!=(128,128): raise ValueError('Native fixture must be 128x128 RGBA')
            data=image.convert('RGBA').tobytes()
    return data, {'path':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--browser-reference', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    result=[]
    for identifier in range(22):
        a,reference=pixels(args.reference,identifier,args.browser_reference)
        b,candidate=pixels(args.candidate,identifier,False)
        errors=[abs(x-y) for x,y in zip(a,b)]
        interior=sorted(abs(a[i+k]-b[i+k]) for i in range(0,len(a),4) if a[i+3]==b[i+3]==255 for k in range(3))
        result.append({'material':identifier,'reference':reference,'candidate':candidate,
            'rgba_mean':sum(errors)/len(errors),'rgba_max':max(errors),
            'alpha_coverage_difference_pixels':sum(a[i+3]!=b[i+3] for i in range(0,len(a),4)),
            'opaque_rgb_mean':sum(interior)/len(interior) if interior else None,
            'opaque_rgb_p99':interior[(len(interior)*99+99)//100-1] if interior else None,
            'opaque_rgb_max':max(interior) if interior else None})
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(args.output)

if __name__=='__main__':main()
