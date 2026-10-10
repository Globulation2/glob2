#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Development-only compact GPU worklist versus production CPU and Frozen8.

This runner cannot consume final holdouts or qualify a production plan. Every
phase starts from fresh immutable seeds; warm repetitions reuse only workspace
capacity and the immutable cost representation, never a previously solved field.
"""
import argparse
import ctypes
import fcntl
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys
import time

from backend import Runner, compile_native
from contracts import HERE, ROOT, sources, sha
from corpus import fields, manifest


def provenance():
    result=sources()
    paths=list((ROOT/'src/field').glob('*.h'))+list((ROOT/'src/map').glob('*.h'))
    paths+=list((ROOT/'src/resource').glob('*.h'))
    paths += [ROOT/'src/common/ComputeExecutor.h',ROOT/'src/common/ThreadCpuClock.h',
              ROOT/'src/map/TerrainRegistry.cpp',
              ROOT/'libgag/include/ThreadSupport.h']
    result.update({str(path.relative_to(ROOT)):sha(path) for path in paths})
    return result


class Comparison:
    def __init__(self,directory,device,groups=0,variants=('compact','compact8')):
        self.gpu=Runner(directory,device,plans={'Frozen8'})
        np,cl=self.gpu.np,self.gpu.cl
        build_started=time.perf_counter_ns();build_cpu=time.process_time_ns()
        self.program=cl.Program(self.gpu.ctx,(HERE/'compact.cl').read_text()).build(['-cl-std=CL1.2'])
        self.compact_builds=[dict(name='compact',source_sha256=sha(HERE/'compact.cl'),options=['-cl-std=CL1.2'],
            wall_ns=time.perf_counter_ns()-build_started,process_cpu_ns=time.process_time_ns()-build_cpu,
            log=self.program.get_build_info(self.gpu.device,cl.program_build_info.LOG))]
        self.kernel=cl.Kernel(self.program,'compact')
        self.gpu.native.run_compact.argtypes=[ctypes.c_void_p]*6+[ctypes.c_uint32]*4+[ctypes.c_void_p]
        self.grouped_program=None;self.grouped_kernel=None
        self.groups=groups or 2*self.gpu.device.max_compute_units
        self.variants=variants
        if 'compact8' in variants:
            # Packed uint/ulong metadata shares the native source ABI. This
            # experimental candidate is explicitly local, little-endian only.
            if sys.byteorder!='little' or not self.gpu.device.endian_little:
                raise ValueError('compact8 development ABI requires little-endian host/device')
            build_started=time.perf_counter_ns();build_cpu=time.process_time_ns()
            self.grouped_program=cl.Program(self.gpu.ctx,(HERE/'compact_grouped.cl').read_text()).build(['-cl-std=CL1.2'])
            self.compact_builds.append(dict(name='compact8',source_sha256=sha(HERE/'compact_grouped.cl'),options=['-cl-std=CL1.2'],
                wall_ns=time.perf_counter_ns()-build_started,process_cpu_ns=time.process_time_ns()-build_cpu,
                log=self.grouped_program.get_build_info(self.gpu.device,cl.program_build_info.LOG)))
            self.grouped_kernel=cl.Kernel(self.grouped_program,'compact_grouped')
            self.gpu.native.run_compact_grouped.argtypes=[ctypes.c_void_p]*6+[ctypes.c_uint32]*5+[ctypes.c_void_p]*2
        self.cpu=ctypes.CDLL(str(directory/'cpu.so'))
        self.cpu.cpu_create.restype=ctypes.c_void_p
        self.cpu.cpu_destroy.argtypes=[ctypes.c_void_p]
        self.cpu.cpu_cost_limit.restype=ctypes.c_uint32
        self.cpu.run_cpu.argtypes=[ctypes.c_void_p]*4+[ctypes.c_uint32]*3+[ctypes.c_void_p]
        self.cpu.cpu_metrics.argtypes=[ctypes.c_void_p]*2

    def execute(self,name,field,scratch):
        np,cl=self.gpu.np,self.gpu.cl
        started=time.perf_counter_ns();cpu_started=time.thread_time_ns();process_started=time.process_time_ns()
        # Counterfactual source arrays are immutable and every call starts from
        # them, including warm reuse after the goals change or disappear.
        if scratch and (scratch['shape']!=(field['width'],field['height']) or scratch['cost_owner'] is not field['costs']):
            self.release(scratch)
        cold=not scratch;details={}
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
            cpu_metrics=np.zeros(8,np.uint64)
            self.cpu.cpu_metrics(scratch['context'],cpu_metrics.ctypes.data)
            details=dict(cpu_variant=('classic_land','classic_water','prepared_profiles','exact_shortcut')[cpu_metrics[0]],
                cpu_buckets=int(cpu_metrics[1]),cpu_profiles=int(cpu_metrics[2]),
                cpu_retained_host_payload_bytes=int(cpu_metrics[3]),cpu_preparation_ns=int(cpu_metrics[4]),
                seed_copied_bytes=int(cpu_metrics[5]),cpu_prepared_now=bool(cpu_metrics[6]),
                cpu_swim=int(cpu_metrics[7]),native_cpu_ns=int(stats[1]*1e6),native_cpu_available=bool(stats[2]))
        elif name in ('compact','compact8'):
            if n*20+40>128*1024*1024:raise ValueError('compact device payload exceeds development 128MiB cap')
            if not scratch:
                mf=cl.mem_flags
                for key,size in (('values',n*4),('costs',n*4),('first',n*4),
                                 ('second',n*4),('stamp',n*4),('count',40 if name=='compact8' else 4)):
                    scratch[key]=cl.Buffer(self.gpu.ctx,mf.READ_WRITE,size)
                cl.enqueue_copy(self.gpu.queue,scratch['costs'],field['costs'],is_blocking=True)
            # Initial frontier construction and both representation conversions
            # are paid on every call; these are not free classification features.
            seeds=field['seeds'].astype(np.uint32)
            initial=np.flatnonzero(seeds>1).astype(np.uint32)
            out=np.empty(n,np.uint32)
            buffers=np.array([scratch[key].int_ptr for key in
                ('values','costs','first','second','stamp','count')],np.uintp)
            details=dict(initial_frontier=int(initial.size),seed_conversion_bytes=n*4,output_conversion_bytes=n*2,
                cost_uploaded_bytes=n*4 if cold else 0,device_workspace_bytes=n*20+(40 if name=='compact8' else 4))
            if name=='compact8':
                counters=np.zeros(14,np.uint64)
                result=self.gpu.native.run_compact_grouped(self.gpu.queue.int_ptr,self.grouped_kernel.int_ptr,
                    buffers.ctypes.data,seeds.ctypes.data,initial.ctypes.data,out.ctypes.data,
                    initial.size,field['width'],field['height'],field['cap'],self.groups,stats.ctypes.data,counters.ctypes.data)
                details.update(dict(zip(('host_checks','processed_items','frontier_highwater','empty_rounds',
                    'seed_uploaded_bytes','frontier_uploaded_bytes','field_readback_bytes','count_readback_bytes',
                    'device_workspace_bytes','argument_updates','count_clears','launched_work_items',
                    'stamp_cleared_bytes','final_frontier'),map(int,counters))))
                details['compact_groups']=self.groups
            else:
                result=self.gpu.native.run_compact(self.gpu.queue.int_ptr,self.kernel.int_ptr,
                    buffers.ctypes.data,seeds.ctypes.data,initial.ctypes.data,out.ctypes.data,
                    initial.size,field['width'],field['height'],field['cap'],stats.ctypes.data)
                details.update(host_checks=int(stats[2]),empty_rounds=0,
                    seed_uploaded_bytes=n*4 if initial.size else 0,frontier_uploaded_bytes=int(initial.size)*4,
                    field_readback_bytes=n*4 if initial.size else 0,count_readback_bytes=int(stats[2])*4)
            out=out.astype(np.uint16);dispatches=int(stats[2])
        else:
            out,timing=self.gpu.execute(name,field,scratch)
            result=0;dispatches=timing['dispatches']
        if result:raise RuntimeError(f'{name} execution failed: {result}')
        scratch['shape']=(field['width'],field['height']);scratch['cost_owner']=field['costs']
        ended=time.perf_counter_ns();cpu_ended=time.thread_time_ns();process_ended=time.process_time_ns()
        if ended<started or cpu_ended<cpu_started or process_ended<process_started:
            raise RuntimeError('reversed diagnostic clock cannot qualify a comparison')
        return out,dict(total_ns=ended-started,
                        host_cpu_ns=cpu_ended-cpu_started,process_cpu_ns=process_ended-process_started,
                        dispatches=dispatches,cold=cold,attribution_complete=False,**details)

    def release(self,scratch):
        if 'context' in scratch:self.cpu.cpu_destroy(scratch.pop('context'))
        scratch.clear()


def verify_edges(runner,output):
    np=runner.gpu.np;count=0
    for w,h in ((1,1),(1,17),(17,1),(7,13),(31,33),(64,65),(1,257),(257,1)):
        for cap in (0,1,40,runner.cpu.cpu_cost_limit(),65533):
            for mode in ('blocked','empty','deferred','dense','bucket128','bucket256','extreme'):
                n=w*h;seeds=np.ones(n,np.uint16)
                costs=np.array([5|(7<<16),10|(14<<16),20|(28<<16),30|(42<<16)],np.uint32)[np.arange(n)%4]
                if mode=='blocked':seeds[:]=0
                if mode=='deferred':
                    seeds[::11]=0;seeds[::7]=65000;seeds[::13]=65535;seeds[-1]=2
                if mode=='dense':seeds[:]=65535
                if mode in ('bucket128','bucket256'):
                    seeds[::13]=65535;seeds[-1]=65000
                    step=70 if mode=='bucket128' else 180
                    costs[:]=step|((step*14//10)<<16)
                if mode=='extreme':seeds[::13]=65535;costs[:]=65535|(1<<16)
                # Independent source changes reuse the same count/list/stamp
                # allocations. Goals disappear/move; no solved output is ever
                # reused as input. Deferred originals outside the cap survive.
                removed=seeds.copy();removed[removed==65535]=1
                moved=removed.copy();allowed=np.flatnonzero(seeds)
                if allowed.size:moved[allowed[-1]]=65535
                cleared=seeds.copy();cleared[cleared>1]=1
                variants=[]
                costs.setflags(write=False)
                for scenario,source in (('initial',seeds),('goals_removed',removed),('goal_moved',moved),('all_sources_removed',cleared)):
                    source.setflags(write=False)
                    field=dict(width=w,height=h,cap=cap,seeds=source,costs=costs)
                    variants.append((scenario,field,runner.gpu.expected(field)))
                # Imported costs >=256 and cap beyond COST_LIMIT are GPU-only
                # oracle checks, explicitly outside the production CPU contract.
                plans=('Frozen8',*runner.variants) if mode=='extreme' or cap>runner.cpu.cpu_cost_limit() else ('cpu','Frozen8',*runner.variants)
                for name in plans:
                    scratch={}
                    try:
                        for scenario,field,expected in variants:
                            original=hashlib.sha256(field['seeds'].tobytes()+costs.tobytes()).hexdigest()
                            actual,timing=runner.execute(name,field,scratch)
                            intact=hashlib.sha256(field['seeds'].tobytes()+costs.tobytes()).hexdigest()==original
                            exact=bool(np.array_equal(actual,expected))
                            row=dict(width=w,height=h,cap=cap,mode=mode,scenario=scenario,plan=name,
                                     exact=exact,original_intact=intact,input_sha256=original,**timing)
                            output.write(json.dumps(row)+'\n');output.flush();count+=1
                            if not exact or not intact:raise RuntimeError(f'edge mismatch: {row}')
                    finally:runner.release(scratch)
    return count


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--lock',type=Path,required=True,help='shared build/measurement lock used by the campaign owner')
    parser.add_argument('--device',type=int,default=0)
    parser.add_argument('--max-size',type=int,choices=(64,128,256,512,1024),default=512)
    parser.add_argument('--repeats',type=int,default=5)
    parser.add_argument('--compact-variant',choices=('both','compact','compact8'),default='both')
    parser.add_argument('--compact-groups',type=int,default=0,help='compact8 fixed grid workgroups; 0 means twice device compute units')
    parser.add_argument('--edges-only',action='store_true',help='correctness-only thin/odd/blocked/deferred/extreme fields')
    args=parser.parse_args()
    if args.device<0 or args.repeats<1 or not 0<=args.compact_groups<=4096:
        parser.error('device must be nonnegative, repeats positive and compact groups 0..4096')
    cases=[] if args.edges_only else [case for case in manifest('development') if case['width']<=args.max_size]
    # Compilation and device setup can disturb somebody else's timing window,
    # so the shared campaign lock precedes every heavy operation, including setup.
    with open(args.lock,'a') as shared_lock:
        fcntl.flock(shared_lock,fcntl.LOCK_EX)
        run_locked(args,cases)


def run_locked(args,cases):
    subprocess.run(['git','diff','--quiet'],cwd=ROOT,check=True)
    subprocess.run(['git','diff','--cached','--quiet'],cwd=ROOT,check=True)
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    args.output.mkdir(parents=True,exist_ok=False)
    directory=args.output.resolve();initial=provenance()
    commands=compile_native(directory)
    command=['g++','-std=c++20','-O3','-DNDEBUG','-fPIC','-shared','-pthread',
             '-I'+str(ROOT/'src'),'-I'+str(ROOT/'src/common'),'-I'+str(ROOT/'libgag/include'),
             str(HERE/'cpu.cpp'),'-o',str(directory/'cpu.so')]
    subprocess.run(command,check=True,capture_output=True,text=True);commands.append(command)
    # This is a development roster, distinct from sealed qualification admission.
    with open(f'/tmp/glob2-gpu-optimization-gpu{args.device}.lock','w') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        variants=('compact','compact8') if args.compact_variant=='both' else (args.compact_variant,)
        runner=Comparison(directory,args.device,args.compact_groups,variants)
        freeze=dict(schema=1,purpose='development screening only; no production admission',
            revision=revision,
            hypothesis='Eight kernel-boundary compact rounds per host count read reduce synchronization CPU versus compact1 while preserving exact sparse atomic worklists; must beat paid strongest compatible production CPU and Frozen8.',
            configuration=dict(max_size=args.max_size,device_index=args.device,compact_workgroup=128,
                compact_host_check_dispatches={'compact':1,'compact8':8},compact8_groups=runner.groups,
                compact8_grid='fixed grid-stride launch, independent of active count',
                compact8_metadata='two counts plus one-writer ulong processed/highwater/empty rounds and guarded overflow',
                list_capacity='cells',deduplication='per-dispatch epoch, reset between independent requests',
                cpu_reference='classic compatible LAND/swim or runtime 64/128/256 direct-profile buckets; production Movement8/256 padding',
                cost_preparation='paid cold, immutable warm',campaign_lock=str(args.lock.resolve()),
                process_cpu='interval diagnostic only; driver work outside interval not attributed; no promotion',
                tracked_device_payload_cap_bytes=128*1024*1024,
                cpu_host_payload='known retained allocations, excludes allocator/control-block overhead and caller arrays',
                attribution_complete=False,production_plans_unchanged=True,final_holdouts_consumed=False),
            sources=initial,corpus=cases,repeats=args.repeats,plans=['cpu','Frozen8',*variants],
            commands=commands,compact_builds=runner.compact_builds,cpu_cost_limit=runner.cpu.cpu_cost_limit(),
            device=runner.gpu.device.name,driver=runner.gpu.device.driver_version,
            compute_units=runner.gpu.device.max_compute_units,device_version=runner.gpu.device.version,
            platform=runner.gpu.device.platform.name,platform_version=runner.gpu.device.platform.version,
            host_byteorder=sys.byteorder,device_little_endian=bool(runner.gpu.device.endian_little),edges_only=args.edges_only)
        (directory/'freeze.json').write_text(json.dumps(freeze,indent=2))
        rng=random.Random(743821);count=0
        with (directory/'results.jsonl').open('w') as output:
            if args.edges_only:count=verify_edges(runner,output)
            for case in cases:
                for original in fields(case):
                    field=dict(original,cap=min(original['cap'],freeze['cpu_cost_limit']))
                    field['seeds'].setflags(write=False);field['costs'].setflags(write=False)
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
                                    intact=hashlib.sha256(field['seeds'].tobytes()+field['costs'].tobytes()).hexdigest()==row['input_sha256']
                                    row.update(timing,exact=bool(runner.gpu.np.array_equal(actual,expected)),original_intact=intact)
                                except Exception as error:row.update(exact=False,error=str(error))
                                output.write(json.dumps(row)+'\n');output.flush();count+=1
                                if not row['exact'] or not row.get('original_intact',False):raise RuntimeError(f'comparison failed: {row}')
                    finally:
                        for storage in scratch.values():runner.release(storage)
                print(json.dumps(dict(layout=case['id'],exact=True)),flush=True)
        if provenance()!=initial:raise RuntimeError('sources changed during development comparison')
        actual={name:dict(records=0,nonempty_dispatch_records=0,dispatches=0,host_checks=0) for name in freeze['plans']}
        with (directory/'results.jsonl').open() as results:
            for line in results:
                row=json.loads(line);summary=actual[row['plan']];summary['records']+=1
                summary['dispatches']+=row.get('dispatches',0);summary['host_checks']+=row.get('host_checks',0)
                summary['nonempty_dispatch_records']+=bool(row.get('dispatches',0))
        if any(not actual[name]['nonempty_dispatch_records'] for name in variants):
            raise RuntimeError('compact candidate was not actually dispatched; incomplete comparison')
        (directory/'complete.json').write_text(json.dumps(dict(records=count,exact=True,
            actual_executions=actual,attribution_complete=False,production_admission=False,
            freeze_sha256=sha(directory/'freeze.json'),results_sha256=sha(directory/'results.jsonl')),indent=2))


if __name__=='__main__':main()
