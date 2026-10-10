#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Development-only barrier frontier: one work-group, bounded pops, exact recovery.

No production dispatch, saved/replay format, sealed qualification protocol or
holdout roster changes. The paid CPU fallback is included in attempted GPU cost.
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

from compare_compact import Comparison, provenance, verify_edges
from contracts import HERE, ROOT, sha
from corpus import fields, manifest

class PersistentComparison(Comparison):
    def __init__(self,directory,device,threads,pops,epochs):
        super().__init__(directory,device,variants=())
        self.variants=('persistent',)
        self.threads,self.pops,self.epochs=threads,pops,epochs
        self.persistent=ctypes.CDLL(str(directory/'persistent.so'))
        self.persistent.run_persistent.argtypes=[ctypes.c_void_p]*5+[ctypes.c_uint32]*6+[ctypes.c_void_p]
        started=time.perf_counter_ns();cpu_started=time.process_time_ns()
        cl=self.gpu.cl
        self.persistent_program=cl.Program(self.gpu.ctx,(HERE/'persistent.cl').read_text()).build(['-cl-std=CL1.2'])
        self.persistent_kernel=cl.Kernel(self.persistent_program,'persistent')
        limit=self.persistent_kernel.get_work_group_info(cl.kernel_work_group_info.WORK_GROUP_SIZE,self.gpu.device)
        if threads>limit:raise ValueError(f'local size {threads} exceeds compiled kernel limit {limit}')
        self.compact_builds.append(dict(name='persistent',source_sha256=sha(HERE/'persistent.cl'),
            options=['-cl-std=CL1.2'],wall_ns=time.perf_counter_ns()-started,
            process_cpu_ns=time.process_time_ns()-cpu_started,
            log=self.persistent_program.get_build_info(self.gpu.device,cl.program_build_info.LOG)))

    def execute(self,name,field,scratch):
        if name!='persistent':
            try:return super().execute(name,field,scratch)
            except Exception:
                self.gpu.queue.finish()
                raise
        started=time.perf_counter_ns();cpu_started=time.thread_time_ns();process_started=time.process_time_ns()
        np,cl=self.gpu.np,self.gpu.cl
        if scratch and (scratch['shape']!=(field['width'],field['height']) or scratch['cost_owner'] is not field['costs']):
            self.release(scratch)
        cold=not scratch;n=field['width']*field['height'];device_bytes=n*22+32
        # This standalone runner retains one Frozen8 and one persistent workspace.
        # Bound their aggregate known GPU payload, including the original upload.
        frozen_bytes=n*8+((field['width']+15)//16)*((field['height']+15)//16)*8+36
        if device_bytes+frozen_bytes>128*1024*1024:raise ValueError('known comparison device payload exceeds128MiB')
        if n*16+256>64*1024*1024:raise ValueError('known immutable/staged/oracle host payload exceeds64MiB')
        if not scratch:
            packed=field['costs']
            if np.any((packed&65535)==0) or np.any((packed>>16)==0):raise ValueError('positive edge costs required')
            for key,size in (('seeds',n*2),('values',n*4),('costs',n*4),('first',n*4),
                             ('second',n*4),('stamp',n*4),('metadata',32)):
                scratch[key]=cl.Buffer(self.gpu.ctx,cl.mem_flags.READ_WRITE,size)
            cl.enqueue_copy(self.gpu.queue,scratch['costs'],packed,is_blocking=True)
        output=np.empty(n,np.uint32);metrics=np.zeros(16,np.uint64)
        buffers=np.array([scratch[key].int_ptr for key in
            ('seeds','values','costs','first','second','stamp','metadata')],np.uintp)
        result=self.persistent.run_persistent(self.gpu.queue.int_ptr,self.persistent_kernel.int_ptr,
            buffers.ctypes.data,field['seeds'].ctypes.data,output.ctypes.data,field['width'],field['height'],
            field['cap'],self.threads,self.pops,self.epochs,metrics.ctypes.data)
        details=dict(zip(('device_status','rounds','pops','frontier_highwater','updates','initial_frontier',
            'remaining_frontier','error_bits','dispatches','host_checks','argument_updates','device_workspace_bytes',
            'seed_uploaded_bytes','metadata_readback_bytes','field_readback_bytes','device_completion_observed'),map(int,metrics)))
        details.update(local_size=self.threads,pop_limit=self.pops,epoch_limit=self.epochs,
            cost_uploaded_bytes=n*4 if cold else 0,seed_conversion_bytes=0,
            tracked_comparison_device_payload_bytes=device_bytes+frozen_bytes,
            known_host_input_staging_oracle_bytes=n*16+256,cpu_fallback=False)
        if result<0:
            # An API rejection may follow the queued resident kernel. Drain
            # BEFORE uint readback/original upload arrays or buffers can die.
            self.gpu.queue.finish()
            raise RuntimeError(f'persistent failed {result}: {details}')
        if result==1:
            # No partial uint output is read or committed. Recover from the
            # original immutable ushort seeds, including paid CPU preparation.
            details['cpu_fallback']=True
            compatible=field['cap']<=self.cpu.cpu_cost_limit() and np.all((field['costs']&65535)<256) and np.all((field['costs']>>16)<256)
            if compatible:
                recovered,timing=super().execute('cpu',field,scratch.setdefault('fallback',{}))
                details['fallback_reference']='strongest compatible production CPU'
                details['fallback_cpu']=timing
            else:
                recovered=self.gpu.expected(field)
                details['fallback_reference']='independent oracle; outside production CPU contract'
            out=recovered
        elif result==0:
            if np.any(output>65535):raise RuntimeError('invalid uint output cannot commit to ushort')
            out=output.astype(np.uint16);details['output_conversion_bytes']=n*2
        else:raise RuntimeError(f'unknown persistent status {result}')
        scratch['shape']=(field['width'],field['height']);scratch['cost_owner']=field['costs']
        end=time.perf_counter_ns();cpu_end=time.thread_time_ns();process_end=time.process_time_ns()
        if end<started or cpu_end<cpu_started or process_end<process_started:raise RuntimeError('invalid diagnostic clocks')
        return out,dict(total_ns=end-started,host_cpu_ns=cpu_end-cpu_started,process_cpu_ns=process_end-process_started,
            cold=cold,attribution_complete=False,**details)

    def release(self,scratch):
        if 'fallback' in scratch:super().release(scratch.pop('fallback'))
        super().release(scratch)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--lock',type=Path,required=True)
    parser.add_argument('--device',type=int,default=0)
    parser.add_argument('--max-size',type=int,choices=(64,128,256,512,1024),default=64)
    parser.add_argument('--local-size',type=int,choices=(64,128,256),default=128)
    parser.add_argument('--pop-limit',type=int,default=262144)
    parser.add_argument('--epoch-limit',type=int,default=65536)
    parser.add_argument('--repeats',type=int,default=5)
    parser.add_argument('--edges-only',action='store_true')
    args=parser.parse_args()
    if args.device<0 or args.repeats<1 or not 1<=args.pop_limit<=1<<24 or not 1<=args.epoch_limit<=65536:
        parser.error('invalid device/repeats or pop/epoch limits')
    with open(args.lock,'a') as campaign:
        fcntl.flock(campaign,fcntl.LOCK_EX)
        run_locked(args)

def run_locked(args):
    subprocess.run(['git','diff','--quiet'],cwd=ROOT,check=True)
    subprocess.run(['git','diff','--cached','--quiet'],cwd=ROOT,check=True)
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    args.output.mkdir(parents=True,exist_ok=False);directory=args.output.resolve();initial=provenance()
    def compile_record(command,label):
        completed=subprocess.run(command,capture_output=True,text=True)
        (directory/f'{label}-compiler.json').write_text(json.dumps(dict(command=command,
            returncode=completed.returncode,stdout=completed.stdout,stderr=completed.stderr),indent=2))
        completed.check_returncode();return command
    commands=[]
    for name in ('native','oracle'):
        command=['g++','-std=c++17','-O3','-DNDEBUG','-fPIC','-shared',
            str(HERE/f'{name}.cpp'),'-o',str(directory/f'{name}.so')]
        if name=='native':command+=['-Wl,-l:libOpenCL.so.1']
        commands.append(compile_record(command,name))
    command=['g++','-std=c++20','-O3','-DNDEBUG','-fPIC','-shared','-pthread',
        '-I'+str(ROOT/'src'),'-I'+str(ROOT/'src/common'),'-I'+str(ROOT/'libgag/include'),
        str(HERE/'cpu.cpp'),'-o',str(directory/'cpu.so')]
    commands.append(compile_record(command,"cpu"))
    command=['g++','-std=c++17','-O3','-DNDEBUG','-fPIC','-shared',str(HERE/'persistent_native.cpp'),
        '-o',str(directory/'persistent.so'),'-Wl,-l:libOpenCL.so.1']
    commands.append(compile_record(command,"persistent"))
    cases=[] if args.edges_only else [case for case in manifest('development') if case['width']<=args.max_size]
    with open(f'/tmp/glob2-gpu-optimization-gpu{args.device}.lock','a') as device_lock:
        fcntl.flock(device_lock,fcntl.LOCK_EX)
        runner=PersistentComparison(directory,args.device,args.local_size,args.pop_limit,args.epoch_limit)
        freeze=dict(schema=1,revision=revision,purpose='isolated development screening; no production admission',
            hypothesis='One bounded barrier-based work-group per field replaces per-round host commands with one kernel enqueue and one metadata read; incomplete work pays exact CPU recovery from immutable originals.',
            configuration=vars(args)|dict(lock=str(args.lock.resolve()),output=str(directory)),
            sources=initial,commands=commands,corpus=cases,plans=['cpu','Frozen8','persistent'],
            builds=runner.compact_builds,device=runner.gpu.device.name,driver=runner.gpu.device.driver_version,
            device_version=runner.gpu.device.version,platform=runner.gpu.device.platform.name,
            native_binary_sha256={name:sha(directory/f'{name}.so') for name in ('native','oracle','cpu','persistent')},
            queue_publication='uniform global/local memory-fence barriers within exactly one work-group',
            cancellation='finite pops/epochs; retain event/storage until terminal; no host interrupt promise',
            production_plans_unchanged=True,final_holdouts_consumed=False,attribution_complete=False,
            storage='known payload only; driver/allocator/control-block RSS excluded; CPU retained workspace reported separately')
        # Paths are source configuration metadata, not JSON-serializable Path objects.
        freeze['configuration']['lock']=str(args.lock.resolve());freeze['configuration']['output']=str(directory)
        (directory/'freeze.json').write_text(json.dumps(freeze,indent=2))
        count=0;rng=random.Random(790451)
        with (directory/'results.jsonl').open('w') as output:
            if args.edges_only:
                count=verify_edges(runner,output)
                # Production caps are far below these imported boundaries. The
                # independent heap oracle is the exact recovery comparator here.
                np=runner.gpu.np
                for cap in (65534,65535,2147483647):
                    for w,h in ((1,257),(7,13)):
                        n=w*h;seeds=np.ones(n,np.uint16);seeds[0]=65535;seeds[-1]=2
                        costs=np.full(n,1|(65535<<16),np.uint32)
                        seeds.setflags(write=False);costs.setflags(write=False)
                        field=dict(width=w,height=h,cap=cap,seeds=seeds,costs=costs)
                        expected=runner.gpu.expected(field);storage={}
                        try:
                            digest=hashlib.sha256(seeds.tobytes()+costs.tobytes()).hexdigest()
                            actual,timing=runner.execute('persistent',field,storage)
                            row=dict(width=w,height=h,cap=cap,mode='imported_cap_boundary',scenario='initial',
                                plan='persistent',input_sha256=digest,exact=bool(np.array_equal(actual,expected)),
                                original_intact=digest==hashlib.sha256(seeds.tobytes()+costs.tobytes()).hexdigest(),**timing)
                            output.write(json.dumps(row)+'\n');output.flush();count+=1
                            if not row['exact'] or not row['original_intact']:raise RuntimeError(f'cap boundary failed: {row}')
                        finally:runner.release(storage)
            for case in cases:
                for original in fields(case):
                    field=dict(original,cap=min(original['cap'],runner.cpu.cpu_cost_limit()))
                    field['seeds'].setflags(write=False);field['costs'].setflags(write=False)
                    expected=runner.gpu.expected(field);scratch={name:{} for name in freeze['plans']}
                    try:
                        for repeat in range(-1,args.repeats):
                            order=list(scratch);rng.shuffle(order)
                            for name in order:
                                digest=hashlib.sha256(field['seeds'].tobytes()+field['costs'].tobytes()).hexdigest()
                                row=dict(layout=case,stage=field['stage'],plan=name,repeat=repeat,cap=field['cap'],input_sha256=digest)
                                try:
                                    actual,timing=runner.execute(name,field,scratch[name])
                                    row.update(timing,exact=bool(runner.gpu.np.array_equal(actual,expected)),
                                        original_intact=digest==hashlib.sha256(field['seeds'].tobytes()+field['costs'].tobytes()).hexdigest())
                                except Exception as error:row.update(exact=False,error=str(error))
                                output.write(json.dumps(row)+'\n');output.flush();count+=1
                                if not row['exact'] or not row.get('original_intact'):raise RuntimeError(f'persistent comparison failed: {row}')
                    finally:
                        for storage in scratch.values():runner.release(storage)
                print(json.dumps(dict(layout=case['id'],exact=True)),flush=True)
        if provenance()!=initial:raise RuntimeError('sources changed during comparison')
        dispatched=complete=bounded=0;maximum_pops=0
        for line in (directory/'results.jsonl').read_text().splitlines():
            row=json.loads(line)
            if row['plan']=='persistent':
                dispatched+=row.get('dispatches',0);complete+=row.get('device_status')==0
                bounded+=row.get('device_status')==1;maximum_pops=max(maximum_pops,row.get('pops',0))
                if row.get('dispatches')!=1 or row.get('host_checks')!=1 or not row.get('device_completion_observed'):
                    raise RuntimeError('missing actual one-dispatch/completion receipt')
        if not dispatched:raise RuntimeError('candidate never dispatched')
        if args.edges_only and args.pop_limit==1 and not bounded:
            raise RuntimeError('tiny-pop screen did not actually exercise exact CPU recovery')
        (directory/'complete.json').write_text(json.dumps(dict(records=count,exact=True,dispatches=dispatched,
            completed_gpu_fields=complete,bounded_cpu_recoveries=bounded,maximum_pops=maximum_pops,
            freeze_sha256=sha(directory/'freeze.json'),results_sha256=sha(directory/'results.jsonl'),
            production_admission=False,attribution_complete=False),indent=2))

if __name__=='__main__':main()
