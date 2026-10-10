#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline OpenCL qualification; no runtime registration of candidates."""
import argparse
import ctypes
import fcntl
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import time

from corpus import digest, fields, manifest, qualifying_class

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CONFIGS = {'Jacobi4': (4, 0, 256, 0), 'Colored2': (2, 1, 128, 0),
           'Colored4': (4, 1, 128, 0), 'Colored8': (8, 1, 256, 0),
           'Frozen8': (8, 1, 128, 1), 'Frozen16': (16, 1, 64, 1)}
CANDIDATES = ('global', 'frontier', 'bounded')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sources():
    paths = sorted(p for p in HERE.iterdir() if p.suffix in ('.py', '.cpp', '.cl', '.json', '.txt'))
    paths += [ROOT/'src/field/OpenCLGradient.cpp']
    return {str(p.relative_to(ROOT)): sha(p) for p in paths}


def compile_native(output):
    commands = []
    for name in ('native', 'oracle'):
        cmd = ['g++', '-std=c++17', '-O3', '-DNDEBUG', '-fPIC', '-shared',
               str(HERE/f'{name}.cpp'), '-o', str(output/f'{name}.so')]
        if name == 'native':
            cmd += ['-Wl,-l:libOpenCL.so.1']
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        commands.append(cmd)
    return commands


class Runner:
    def __init__(self, directory, device_index):
        import numpy as np
        import pyopencl as cl
        self.np, self.cl = np, cl
        devices = [d for p in cl.get_platforms() for d in p.get_devices(device_type=cl.device_type.GPU)]
        if not 0 <= device_index < len(devices):
            raise ValueError(f'GPU index {device_index} unavailable; found {len(devices)} GPUs')
        self.device = devices[device_index]
        self.ctx = cl.Context([self.device]); self.queue = cl.CommandQueue(self.ctx)
        self.native = ctypes.CDLL(str(directory/'native.so'))
        ptr, uint = ctypes.c_void_p, ctypes.c_uint32
        self.native.run_native.argtypes = [ptr]*6 + [uint]*5 + [ptr]
        self.native.run_frontier.argtypes = [ptr]*9 + [uint]*3 + [ptr]
        self.oracle = ctypes.CDLL(str(directory/'oracle.so')).oracle
        self.oracle.argtypes = [ptr]*3 + [ctypes.c_int]*3
        self.kernels, self.programs = {}, []
        self.bounded_kernels = {}
        production = (ROOT/'src/field/OpenCLGradient.cpp').read_text().split('R"CL(', 1)[1].split(')CL";', 1)[0]
        builds = []
        for build_name in (*CONFIGS, 'global', 'frontier', 'bounded2', 'bounded4', 'bounded8', 'bounded16'):
            name = 'bounded' if build_name.startswith('bounded') else build_name
            source = production if name in CONFIGS or name == 'bounded' else (HERE/f'{name}.cl').read_text()
            options = ['-cl-std=CL1.2']
            if name in CONFIGS or name == 'bounded':
                steps, color, threads, frozen = CONFIGS.get(name, (int(build_name[7:]) if name == 'bounded' else 16, 0, 256, 0))
                options += [f'-D{k}={v}' for k,v in dict(CORE_X=16,CORE_Y=16,
                            STEPS=steps,COLOR_RELAXATION=color,WG=threads,FROZEN_HALO=frozen).items()]
            start = time.perf_counter_ns()
            program = cl.Program(self.ctx, source).build(options=options)
            self.programs.append(program)
            self.kernels[name] = cl.Kernel(program, 'frontier' if name == 'frontier' else 'propagate')
            if name == 'bounded':
                self.bounded_kernels[steps] = self.kernels[name]
            builds.append(dict(name=build_name, options=options, compilation_ns=time.perf_counter_ns()-start,
                               source_sha256=hashlib.sha256(source.encode()).hexdigest(),
                               log=program.get_build_info(self.device, cl.program_build_info.LOG)))
        (directory/'builds.json').write_text(json.dumps(builds, indent=2))

    def expected(self, field):
        out = self.np.empty_like(field['seeds'])
        self.oracle(field['seeds'].ctypes.data, field['costs'].ctypes.data,
                    out.ctypes.data, field['width'], field['height'], field['cap'])
        return out

    def execute(self, name, field, scratch):
        """Timer covers dispatch-specific prep, cold buffers, uploads and readback.

        Warm cost planes are immutable and reused. Cold includes cost uploads;
        source generation and independent verification lie outside timing.
        Python control/binding overhead is INCLUDED; this is an offline screen,
        not a production performance or admission claim.
        """
        np, cl = self.np, self.cl
        started = time.perf_counter_ns()
        w, h, cap = field['width'], field['height'], field['cap']; n = w*h
        frontier = name == 'frontier'; size = n*(4 if frontier else 2)
        if not scratch:
            mf = cl.mem_flags
            scratch['a'] = cl.Buffer(self.ctx, mf.READ_WRITE, size)
            scratch['b'] = scratch['a'] if frontier else cl.Buffer(self.ctx, mf.READ_WRITE, size)
            scratch['c'] = cl.Buffer(self.ctx, mf.READ_ONLY, n*4)
            scratch['desc'] = cl.Buffer(self.ctx, mf.READ_ONLY, 32)
            scratch['flag'] = cl.Buffer(self.ctx, mf.READ_WRITE, 4)
            mask = n if frontier else ((w+15)//16)*((h+15)//16)
            scratch['m0'] = cl.Buffer(self.ctx, mf.READ_WRITE, mask*4)
            scratch['m1'] = cl.Buffer(self.ctx, mf.READ_WRITE, mask*4)
            cl.enqueue_copy(self.queue, scratch['c'], field['costs'], is_blocking=True)
            # Existing production preparation already recognizes uniform planes.
            scratch['minimum_step'] = min(int(np.min(field['costs'] & 65535)), int(np.min(field['costs'] >> 16))) if name == 'bounded' else 0
            scratch['uniform'] = False if frontier else bool(np.all(field['costs'] == field['costs'][0]))
        seeds = field['seeds'].astype(np.uint32) if frontier else field['seeds']
        out = np.empty_like(seeds); stats = np.zeros(3, np.float64)
        s = scratch
        prep_ns = time.perf_counter_ns()-started
        if frontier:
            result = self.native.run_frontier(self.queue.int_ptr, self.kernels[name].int_ptr,
                     *[s[k].int_ptr for k in ('a','c','m0','m1','flag')],
                     seeds.ctypes.data, out.ctypes.data, w, h, cap, stats.ctypes.data)
        else:
            desc = np.array([w,h,0,0,cap,3 if s['uniform'] else 1,(w+15)//16,(h+15)//16],np.uint32)
            bufs = np.array([s['a'].int_ptr,s['b'].int_ptr,*([s['c'].int_ptr]*8),
                             s['flag'].int_ptr,s['desc'].int_ptr,s['m0'].int_ptr,s['m1'].int_ptr],np.uintp)
            steps, _, threads, _ = CONFIGS.get(name,(16,0,256,0) if name == 'bounded' else (1,0,128,0))
            if name == 'bounded' and (cap > 16 * scratch['minimum_step']):
                raise ValueError('ineligible bounded plan')
            kernel = self.kernels[name]
            if name == 'bounded':
                required = max(1, cap//scratch['minimum_step'])
                steps = next(s for s in (2,4,8,16) if s >= required)
                kernel = self.bounded_kernels[steps]
            result = self.native.run_native(self.queue.int_ptr,kernel.int_ptr,
                     bufs.ctypes.data,seeds.ctypes.data,desc.ctypes.data,out.ctypes.data,
                     n,w,h,threads,int(name == 'bounded'),stats.ctypes.data)
        # Include conversion back to the production ushort output representation.
        if frontier:
            out = out.astype(np.uint16)
        elapsed = time.perf_counter_ns()-started
        if result:
            raise RuntimeError(f'{name}: OpenCL/convergence failure {result}')
        return out, dict(total_ns=elapsed, preparation_ns=prep_ns,
                         local_steps=steps if not frontier else 0,
                         execution_ns=int(stats[0]*1e6),dispatches=int(stats[2]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--split', choices=('development','final','stress'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--device', type=int, default=0)
    parser.add_argument('--development-report', type=Path)
    parser.add_argument('--stress-report', type=Path)
    parser.add_argument('--edge-report', type=Path)
    args = parser.parse_args()
    cases = manifest(args.split)
    protocol = json.loads((HERE/'protocol.json').read_text())
    if args.device < 0:
        parser.error('device index must be nonnegative')
    if args.split == 'final':
        if not all((args.development_report,args.stress_report,args.edge_report)):
            parser.error('final requires complete development, stress and edge reports')
        from analyze import analyze
        prior = analyze(args.development_report.parent)
        if prior != json.loads(args.development_report.read_text()):
            parser.error('development report differs from its complete raw evidence')
        if prior.get('split') != 'development' or not prior.get('screen_survivors'):
            parser.error('no development candidate passed screening')
        stress = analyze(args.stress_report.parent)
        edges = json.loads(args.edge_report.read_text())
        if stress != json.loads(args.stress_report.read_text()) or stress['split'] != 'stress':
            parser.error('stress report differs from its complete raw evidence')
        if not edges.get('exact') or edges.get('cases',0) < 120:
            parser.error('adversarial edge coverage is incomplete')
        if any(report.get('sources') != sources() for report in (prior,stress,edges)):
            parser.error('source changed since development, stress or edge verification')
    if args.split == 'final':
        ledger = ROOT/'artifacts/gradient-qualification/consumed-holdouts'
        ledger.mkdir(parents=True, exist_ok=True)
        try:
            with (ledger/(digest(cases)+'.json')).open('x') as receipt:
                json.dump(dict(output=str(args.output.resolve()), sources=sources(),
                               consumed_unix=time.time()), receipt, indent=2)
        except FileExistsError:
            parser.error('these holdouts have already been consumed; do not tune and retry')
    args.output.mkdir(parents=True, exist_ok=False)
    args.output = args.output.resolve()
    freeze = dict(schema=1,split=args.split,protocol=protocol,corpus=cases,
                  corpus_sha256=digest(cases),sources=sources(),created_unix=time.time(),
                  affinity=sorted(os.sched_getaffinity(0)),required_integration_compute_slots=8,
                  note='Offline single-queue kernel study, not an executor/whole-game benchmark')
    (args.output/'freeze.json').write_text(json.dumps(freeze,indent=2))
    commands = compile_native(args.output)
    # Cooperative lock prevents overlap with this runner; does not imply exclusive GPU ownership.
    with open(f'/tmp/glob2-gpu-optimization-gpu{args.device}.lock','w') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        runner = Runner(args.output,args.device)
        device = runner.device
        metadata = dict(device=device.name,driver=device.driver_version,platform=device.platform.name,
                        python=os.sys.version,commands=commands,
                        compiler=subprocess.check_output(['g++','--version'],text=True),
                        numpy=runner.np.__version__,pyopencl=runner.cl.VERSION_TEXT)
        (args.output/'environment.json').write_text(json.dumps(metadata,indent=2))
        rng = random.Random(918273)
        with (args.output/'results.jsonl').open('w') as output:
            for index, layout in enumerate(cases):
                for field in fields(layout):
                    expected = runner.expected(field)
                    scratch = {name:{} for name in runner.kernels}
                    for repeat in range(-1,protocol['warm_repetitions']):
                        order = list(runner.kernels); rng.shuffle(order)
                        for name in order:
                            record = dict(layout=layout,stage=field['stage'],plan=name,repeat=repeat,
                                          cap=field['cap'],seed_count=field['seed_count'],
                                          cost_classes=field['cost_classes'],minimum_step=field['minimum_step'],obstacle_count=field['obstacle_count'],
                                          class_match=qualifying_class(name,field) if name in CANDIDATES else False,
                                          input_sha256=hashlib.sha256(field['seeds'].tobytes()+field['costs'].tobytes()).hexdigest())
                            if name == 'bounded' and (field['cap'] > 16 * field['minimum_step']):
                                record.update(eligible=False,exact=None,reason='outside semantic cap/step bound')
                                output.write(json.dumps(record)+'\n');output.flush()
                                continue
                            record['eligible'] = True
                            try:
                                actual, timing = runner.execute(name,field,scratch[name])
                                record.update(timing,exact=bool(runner.np.array_equal(actual,expected)))
                                if not record['exact']:
                                    record['mismatches'] = int(runner.np.count_nonzero(actual != expected))
                            except Exception as error:
                                record.update(exact=False,error=str(error))
                            output.write(json.dumps(record)+'\n');output.flush()
                            if not record['exact']:
                                raise RuntimeError(f'qualification failed: {record}')
                print(json.dumps(dict(layout=index+1,total=len(cases),exact=True)),flush=True)
    if sources() != freeze['sources']:
        raise RuntimeError('source changed during qualification; results are invalid')
    (args.output/'complete.json').write_text(json.dumps(dict(layouts=len(cases),completed_unix=time.time())))


if __name__ == '__main__':
    main()
