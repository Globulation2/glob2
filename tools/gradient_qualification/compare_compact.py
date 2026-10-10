#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Development-only compact GPU worklist versus production CPU and Frozen8.

This runner cannot consume final holdouts or qualify a production plan. Every
phase starts from fresh immutable seeds; warm repetitions reuse only workspace
capacity and the immutable cost representation, never a previously solved field.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import random
import subprocess
import time

from backend import Runner, compile_native
from contracts import HERE, ROOT, sources, sha
from corpus import fields, manifest


def provenance():
    result=sources()
    paths=list((ROOT/'src/field').glob('*.h'))+list((ROOT/'src/map').glob('*.h'))
    paths+=list((ROOT/'src/resource').glob('*.h'))
    paths += [ROOT/'src/common/ComputeExecutor.h',ROOT/'src/common/ThreadCpuClock.h',
              ROOT/'libgag/include/ThreadSupport.h']
    result.update({str(path.relative_to(ROOT)):sha(path) for path in paths})
    return result


class Comparison:
    def __init__(self,directory,device):
        self.gpu=Runner(directory,device,plans={'Frozen8'})
        np,cl=self.gpu.np,self.gpu.cl
        self.program=cl.Program(self.gpu.ctx,(HERE/'compact.cl').read_text()).build(['-cl-std=CL1.2'])
        self.kernel=cl.Kernel(self.program,'compact')
        self.gpu.native.run_compact.argtypes=[ctypes.c_void_p]*6+[ctypes.c_uint32]*4+[ctypes.c_void_p]
        self.cpu=ctypes.CDLL(str(directory/'cpu.so'))
        self.cpu.cpu_create.restype=ctypes.c_void_p
        self.cpu.cpu_destroy.argtypes=[ctypes.c_void_p]
        self.cpu.cpu_cost_limit.restype=ctypes.c_uint32
        self.cpu.run_cpu.argtypes=[ctypes.c_void_p]*4+[ctypes.c_uint32]*3+[ctypes.c_void_p]

    def execute(self,name,field,scratch):
        np,cl=self.gpu.np,self.gpu.cl
        started=time.perf_counter_ns();cpu_started=time.thread_time_ns()
        stats=np.zeros(3,np.float64);n=field['width']*field['height']
        if name=='cpu':
            if not scratch:
                scratch['context']=self.cpu.cpu_create()
                if not scratch['context']:raise RuntimeError('CPU workspace allocation failed')
            out=np.empty_like(field['seeds'])
            result=self.cpu.run_cpu(scratch['context'],field['seeds'].ctypes.data,
                field['costs'].ctypes.data,out.ctypes.data,field['width'],field['height'],
                field['cap'],stats.ctypes.data)
            dispatches=0
        elif name=='compact':
            if not scratch:
                mf=cl.mem_flags
                for key,size in (('values',n*4),('costs',n*4),('first',n*4),
                                 ('second',n*4),('stamp',n*4),('count',4)):
                    scratch[key]=cl.Buffer(self.gpu.ctx,mf.READ_WRITE,size)
                cl.enqueue_copy(self.gpu.queue,scratch['costs'],field['costs'],is_blocking=True)
            # Initial frontier construction and both representation conversions
            # are paid on every call; these are not free classification features.
            seeds=field['seeds'].astype(np.uint32)
            initial=np.flatnonzero(seeds>1).astype(np.uint32)
            out=np.empty(n,np.uint32)
            buffers=np.array([scratch[key].int_ptr for key in
                ('values','costs','first','second','stamp','count')],np.uintp)
            result=self.gpu.native.run_compact(self.gpu.queue.int_ptr,self.kernel.int_ptr,
                buffers.ctypes.data,seeds.ctypes.data,initial.ctypes.data,out.ctypes.data,
                initial.size,field['width'],field['height'],field['cap'],stats.ctypes.data)
            out=out.astype(np.uint16);dispatches=int(stats[2])
        else:
            out,timing=self.gpu.execute(name,field,scratch)
            result=0;dispatches=timing['dispatches']
        if result:raise RuntimeError(f'{name} execution failed: {result}')
        return out,dict(total_ns=time.perf_counter_ns()-started,
                        host_cpu_ns=time.thread_time_ns()-cpu_started,dispatches=dispatches)

    def release(self,scratch):
        if 'context' in scratch:self.cpu.cpu_destroy(scratch.pop('context'))
        scratch.clear()


def verify_edges(runner,output):
    np=runner.gpu.np;count=0
    for w,h in ((1,1),(1,17),(17,1),(7,13),(31,33),(64,65)):
        for cap in (0,1,40,runner.cpu.cpu_cost_limit()):
            for mode in ('blocked','empty','deferred','extreme'):
                n=w*h;seeds=np.ones(n,np.uint16)
                costs=np.array([5|(7<<16),10|(14<<16),20|(28<<16),30|(42<<16)],np.uint32)[np.arange(n)%4]
                if mode=='blocked':seeds[:]=0
                if mode=='deferred':
                    seeds[::11]=0;seeds[::7]=65000;seeds[::13]=65535;seeds[-1]=2
                if mode=='extreme':seeds[::13]=65535;costs[:]=65535|(1<<16)
                field=dict(width=w,height=h,cap=cap,seeds=seeds,costs=costs)
                expected=runner.gpu.expected(field)
                # The production 64-bucket comparator does not support extreme
                # arbitrary imported costs; GPU exactness still uses the oracle.
                plans=('Frozen8','compact') if mode=='extreme' else ('cpu','Frozen8','compact')
                for name in plans:
                    scratch={}
                    try:
                        actual,_=runner.execute(name,field,scratch)
                        exact=bool(np.array_equal(actual,expected))
                        row=dict(width=w,height=h,cap=cap,mode=mode,plan=name,exact=exact)
                        output.write(json.dumps(row)+'\n');output.flush();count+=1
                        if not exact:raise RuntimeError(f'edge mismatch: {row}')
                    finally:runner.release(scratch)
    return count


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--device',type=int,default=0)
    parser.add_argument('--max-size',type=int,choices=(64,128,256,512,1024),default=512)
    parser.add_argument('--repeats',type=int,default=5)
    parser.add_argument('--edges-only',action='store_true',help='correctness-only thin/odd/blocked/deferred/extreme fields')
    args=parser.parse_args()
    if args.device<0 or args.repeats<1:parser.error('device must be nonnegative and repeats positive')
    cases=[] if args.edges_only else [case for case in manifest('development') if case['width']<=args.max_size]
    args.output.mkdir(parents=True,exist_ok=False)
    directory=args.output.resolve();initial=provenance()
    commands=compile_native(directory)
    command=['g++','-std=c++20','-O3','-DNDEBUG','-fPIC','-shared','-pthread',
             '-I'+str(ROOT/'src'),'-I'+str(ROOT/'src/common'),'-I'+str(ROOT/'libgag/include'),
             str(HERE/'cpu.cpp'),'-o',str(directory/'cpu.so')]
    subprocess.run(command,check=True,capture_output=True,text=True);commands.append(command)
    # This is a development roster, distinct from sealed qualification admission.
    import fcntl
    with open(f'/tmp/glob2-gpu-optimization-gpu{args.device}.lock','w') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        runner=Comparison(directory,args.device)
        freeze=dict(schema=1,purpose='development screening only; no production admission',
            sources=initial,corpus=cases,repeats=args.repeats,plans=['cpu','Frozen8','compact'],
            commands=commands,cpu_cost_limit=runner.cpu.cpu_cost_limit(),
            device=runner.gpu.device.name,driver=runner.gpu.device.driver_version,edges_only=args.edges_only)
        (directory/'freeze.json').write_text(json.dumps(freeze,indent=2))
        rng=random.Random(743821);count=0
        with (directory/'results.jsonl').open('w') as output:
            if args.edges_only:count=verify_edges(runner,output)
            for case in cases:
                for original in fields(case):
                    field=dict(original,cap=min(original['cap'],freeze['cpu_cost_limit']))
                    expected=runner.gpu.expected(field)
                    scratch={name:{} for name in freeze['plans']}
                    try:
                        for repeat in range(-1,args.repeats):
                            order=list(freeze['plans']);rng.shuffle(order)
                            for name in order:
                                row=dict(layout=case,stage=field['stage'],plan=name,repeat=repeat,
                                    cap=field['cap'],original_fixture_cap=original['cap'],
                                    input_sha256=hashlib.sha256(field['seeds'].tobytes()+field['costs'].tobytes()).hexdigest())
                                try:
                                    actual,timing=runner.execute(name,field,scratch[name])
                                    row.update(timing,exact=bool(runner.gpu.np.array_equal(actual,expected)))
                                except Exception as error:row.update(exact=False,error=str(error))
                                output.write(json.dumps(row)+'\n');output.flush();count+=1
                                if not row['exact']:raise RuntimeError(f'comparison failed: {row}')
                    finally:
                        for storage in scratch.values():runner.release(storage)
                print(json.dumps(dict(layout=case['id'],exact=True)),flush=True)
        if provenance()!=initial:raise RuntimeError('sources changed during development comparison')
        (directory/'complete.json').write_text(json.dumps(dict(records=count,exact=True,
            freeze_sha256=sha(directory/'freeze.json'),results_sha256=sha(directory/'results.jsonl')),indent=2))


if __name__=='__main__':main()
