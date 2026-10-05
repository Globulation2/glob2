#!/usr/bin/env python3
"""Build/run opt-in paired terrain gradient benchmarks; no timing gates in CI.

Baseline is an explicit source tree captured before optimization. Its generalized
bucket function is compiled into the SAME executable, with only its name, array
extent, and optional counters changed. Both solvers use identical buffers, inputs,
compiler, flags, queue implementation, and timer code. An independent heap oracle
checks every result. Shared queues measure warm reuse; repetition -1 is cold.
"""
from __future__ import annotations
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = ('classic', 'road', 'ice', 'sparse', 'network', 'dense', 'isolated')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def baseline_header(source):
    text = source.read_text()
    start = text.index('template<class TerrainAt>')
    end = text.index('\ntemplate<class TerrainAt>', start + 1)
    text = text[start:end]
    text = text.replace('template<class TerrainAt>', 'template<class TerrainAt, std::size_t N>')
    text = text.replace('expandTerrainBucket', 'expandBaselineTerrainBucket')
    text = text.replace('TERRAIN_COUNT', 'N').replace('const TerrainEntryCosts &costs', 'const std::array<EntrySteps,N> &costs')
    text = text.replace('if (!count) return;', 'if (!count) return; GLOB2_GRADIENT_BENCH_EVENT(occupied,1); GLOB2_GRADIENT_BENCH_EVENT(popped,count);')
    text = text.replace('target.reserveExtra(', 'GLOB2_GRADIENT_BENCH_EVENT(chunkReserves,1); target.reserveExtra(')
    text = text.replace('if (gradient[i] != GRADIENT_AT_GOAL - cur) continue;', 'if (gradient[i] != GRADIENT_AT_GOAL - cur) { GLOB2_GRADIENT_BENCH_EVENT(stale,1); continue; }')
    text = text.replace('pending += newSize-target.size;', 'GLOB2_GRADIENT_BENCH_EVENT(relaxations,newSize-target.size); pending += newSize-target.size;')
    return '// Generated frozen comparison adapter; do not edit.\nnamespace gradient_kernel {\n' + text + '\n}\n'


def cases(suite):
    # Exhaustive combinations stay opt-in. The representative set balances each
    # requested axis without an unnecessarily huge Cartesian product.
    sizes = (32,64,128,256,512) if suite == 'full' else (32,128,512) if suite == 'representative' else (32,)
    for size, pattern, swim in itertools.product(sizes, PATTERNS, range(7)):
        yield dict(size=size, pattern=pattern, swim=swim, registry=7, costs='equivalent', seeds='single', mode='terrain')
    for size, swim in itertools.product(sizes, range(7)):
        yield dict(size=size,pattern='classic',swim=swim,registry=7,costs='equivalent',seeds='single',mode='dispatch')
    for size, pattern, travel in itertools.product(sizes, PATTERNS, (1,2,3)):
        yield dict(size=size,pattern=pattern,swim=3,registry=7,costs='equivalent',seeds='dense',mode='strategic',travel=travel)
    for registry, costs, seeds, mode in itertools.product((8,32,64), ('equivalent','distinct'), ('single','dense','deferred'), ('terrain','plane')):
        yield dict(size=128 if suite != 'smoke' else 32, pattern='dense', swim=3, registry=registry,costs=costs,seeds=seeds,mode=mode)
    for width,height,swim in itertools.product((1,2,3,17),(1,3,31),range(7)):
        yield dict(width=width,height=height,pattern='dense',swim=swim,registry=8,costs='equivalent',seeds='deferred',cap=127,mode='terrain')
    for cap,seeds in itertools.product((0,1,42,63,64,65,250,701),('single','dense','deferred')):
        yield dict(size=32,pattern='dense',swim=6,registry=64,costs='distinct',seeds=seeds,cap=cap,mode='terrain')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline-dir',required=True,type=Path,help='frozen baseline source root containing field/TerrainGradient.h')
    ap.add_argument('--candidate-dir',type=Path,default=ROOT/'src')
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--suite',choices=('smoke','representative','full'),default='representative')
    ap.add_argument('--repeats',type=int,default=11)
    ap.add_argument('--compiler',default=os.environ.get('CXX','g++'))
    ap.add_argument('--flags',default='-O3 -DNDEBUG -std=c++20')
    ap.add_argument('--scalar',action='store_true')
    ap.add_argument('--instrumented',action='store_true',help='collect counters/allocations; never use these timings for performance claims')
    ap.add_argument('--cpu',type=int,help='pin benchmark subprocess to a CPU')
    ap.add_argument('--bucket-count',type=int,choices=(64,256),help='standalone future-cost experiment: override queue ring in copied headers only')
    ap.add_argument('--build-only',action='store_true')
    ap.add_argument('--case',action='append',help='JSON case object; repeat for a custom matrix')
    args=ap.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    baseline=args.baseline_dir/'field/TerrainGradient.h'
    generated=args.output/'baseline_gradient.h';generated.write_text(baseline_header(baseline))
    travel=(args.baseline_dir/'field/TerrainTravel.h').read_text()
    travel=travel[travel.index('namespace field'):].replace('namespace field','namespace baseline_field',1)
    travel=travel.replace('const auto [cost,index]=queue.top();queue.pop();','const auto [cost,index]=queue.top();queue.pop(); GLOB2_GRADIENT_BENCH_EVENT(popped,1);')
    travel=travel.replace('if(cost!=costs[index]) continue;','if(cost!=costs[index]) { GLOB2_GRADIENT_BENCH_EVENT(stale,1); continue; }')
    travel=travel.replace('costs[next]=candidate;queue.emplace(candidate,next);','costs[next]=candidate;queue.emplace(candidate,next); GLOB2_GRADIENT_BENCH_EVENT(relaxations,1);')
    (args.output/'baseline_travel.h').write_text(travel)
    snapshot=args.output/'source'
    (snapshot/'field').mkdir(parents=True,exist_ok=True)
    (snapshot/'map').mkdir(exist_ok=True)
    for header in (args.candidate_dir/'field').glob('*.h'):shutil.copyfile(header,snapshot/'field'/header.name)
    for name in ('TerrainProperties.h','TerrainType.h'):shutil.copyfile(args.candidate_dir/'map'/name,snapshot/'map'/name)
    shutil.copyfile(ROOT/'tools/gradient_benchmark.cpp',snapshot/'gradient_benchmark.cpp')
    shutil.copyfile(__file__,snapshot/'gradient_benchmark.py')
    if args.bucket_count:
        bucket=snapshot/'field/GradientBucket.h'
        text=bucket.read_text(); start=text.index('static constexpr unsigned COUNT ='); end=text.index('();',start)+3
        text=text[:start]+f'static constexpr unsigned COUNT = {args.bucket_count};'+text[end:];bucket.write_text(text)
    binary=args.output/'gradient-benchmark'
    command=[args.compiler,*shlex.split(args.flags),'-I'+str(snapshot),'-I'+str(args.output),str(snapshot/'gradient_benchmark.cpp'),'-o',str(binary)]
    if args.scalar:command.insert(1,'-DGLOB2_GRADIENT_SCALAR')
    if args.instrumented:command.insert(1,'-DGLOB2_GRADIENT_BENCH_COUNTERS')
    subprocess.run(command,check=True)
    manifest=dict(command=command,compiler=subprocess.check_output([args.compiler,'--version'],text=True),platform=platform.platform(),cpu=args.cpu,instrumented=args.instrumented,baseline_source_sha256=sha(baseline),adapter_sha256=sha(generated),baseline_travel_source_sha256=sha(args.baseline_dir/'field/TerrainTravel.h'),baseline_travel_adapter_sha256=sha(args.output/'baseline_travel.h'),benchmark_sha256=sha(ROOT/'tools/gradient_benchmark.cpp'),binary_sha256=sha(binary),candidate_headers={str(p.relative_to(snapshot)):sha(p) for p in sorted(snapshot.rglob('*.h'))},args=vars(args))
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2,default=str)+'\n')
    if args.build_only:return
    matrix=[json.loads(c) for c in args.case] if args.case else list(cases(args.suite))
    summary=[]
    with (args.output/'samples.jsonl').open('w') as raw:
        for index,case in enumerate(matrix):
            for layout in ('shared','separate'):
                cmd=[str(binary),'--repeats',str(args.repeats),'--layout',layout]
                for k,v in case.items():cmd.extend(['--'+k,str(v)])
                if args.cpu is not None:cmd=['taskset','-c',str(args.cpu),*cmd]
                result=subprocess.run(cmd,check=True,text=True,capture_output=True)
                raw.write(result.stdout);raw.flush()
                rows=[json.loads(line) for line in result.stdout.splitlines()]
                warm=[r for r in rows if r['repetition']>=0]
                before=statistics.median(r['cpu_ms'] for r in warm if not r['candidate'])
                after=statistics.median(r['cpu_ms'] for r in warm if r['candidate'])
                summary.append(dict(case=case,layout=layout,baseline_cpu_ms=before,candidate_cpu_ms=after,ratio=after/before if before else None,command=cmd))
            if index%20==0:print(f'{index+1}/{len(matrix)} cases',file=sys.stderr,flush=True)
    (args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')

if __name__=='__main__':main()
