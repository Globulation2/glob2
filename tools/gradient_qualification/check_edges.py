#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Correctness-only adversarial fields, separate from randomized performance."""
import argparse
import fcntl
import json
from pathlib import Path
import numpy as np
from backend import Runner, compile_native
from contracts import sources


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--device',type=int,default=0)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    directory=args.output.resolve();initial=sources();compile_native(directory)
    count=0;cases=0;rng=np.random.Generator(np.random.PCG64(984723))
    with open(f'/tmp/glob2-gpu-optimization-gpu{args.device}.lock','w') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX);runner=Runner(directory,args.device)
        for w,h in ((1,1),(1,17),(17,1),(7,13),(31,33),(64,65)):
            for cap in (0,1,10,40,65533):
                for mode in ('blocked','empty','deferred','extreme'):
                    n=w*h;seeds=np.ones(n,np.uint16)
                    cardinal=rng.integers(1,100,n,dtype=np.uint32)
                    diagonal=rng.integers(1,100,n,dtype=np.uint32)
                    if mode=='blocked':seeds[:]=0
                    if mode=='deferred':
                        seeds[rng.random(n)<.3]=0
                        seeds[::7]=65000
                        seeds[::13]=65535
                        seeds[-1]=2
                    if mode=='extreme':
                        seeds[::13]=65535
                        cardinal[:]=65535;diagonal[:]=1
                    field=dict(width=w,height=h,cap=cap,seeds=seeds,costs=cardinal|(diagonal<<16))
                    expected=runner.expected(field);cases+=1
                    if mode in ('blocked','empty'):
                        if not np.array_equal(expected,seeds):
                            raise RuntimeError('oracle modified seedless field')
                    for name in runner.kernels:
                        if name=='bounded' and cap>16*min(int(cardinal.min()),int(diagonal.min())):
                            continue
                        actual,_=runner.execute(name,field,{})
                        if not np.array_equal(actual,expected):
                            raise RuntimeError(f'edge mismatch: {name}/{w}x{h}/{mode}/{cap}')
                        count+=1
        # Directed source-cell costs and thin toroidal neighbor aliases.
        field=dict(width=3,height=1,cap=10,seeds=np.array([65535,1,1],np.uint16),
                   costs=np.array([10|(14<<16),20|(28<<16),30|(42<<16)],np.uint32))
        if runner.expected(field).tolist()!=[65535,65525,65525]:
            raise RuntimeError('oracle directed-cost sanity check failed')
        if sources()!=initial:
            raise RuntimeError('source changed during edge verification')
        result=dict(exact=True,cases=cases,executions=count,sources=initial,
                    device=runner.device.name,driver=runner.device.driver_version)
        (directory/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(dict(cases=cases,executions=count,exact=True)))


if __name__=='__main__':main()
