#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Orchestrate frozen offline qualification; never register runtime candidates."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import time

from corpus import digest, fields, manifest, qualifying_class
from contracts import (HERE, ROOT, CANDIDATES, sources, load_protocol, finish_run,
                       consume_holdouts, sha)
from backend import Runner, compile_native

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
    protocol = load_protocol()
    selected = list(CANDIDATES)
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
        if edges.get('exact') is not True or edges.get('cases') != 120 or edges.get('executions') != 1039:
            parser.error('adversarial edge coverage is incomplete')
        selected = prior['screen_survivors']
        if any(report.get('sources') != sources() for report in (prior,stress,edges)):
            parser.error('source changed since development, stress or edge verification')
    args.output.mkdir(parents=True, exist_ok=False)
    args.output = args.output.resolve()
    freeze = dict(schema=1,split=args.split,protocol=protocol,corpus=cases,candidates=selected,
                  corpus_sha256=digest(cases),sources=sources(),created_unix=time.time(),
                  affinity=sorted(os.sched_getaffinity(0)),required_integration_compute_slots=8,
                  note='Offline single-queue kernel study, not an executor/whole-game benchmark')
    if args.split == 'final':
        path = args.output/'development-analysis.json'
        path.write_text(json.dumps(prior,indent=2))
        freeze['development_report_sha256'] = sha(path)
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
        if sources() != freeze['sources']:
            raise RuntimeError('source changed during setup')
        if args.split == 'final':
            try:
                consume_holdouts(ROOT/'artifacts/gradient-qualification/consumed-holdouts', cases, args.output)
            except FileExistsError:
                parser.error('these holdouts have already been consumed; do not tune and retry')
        records = executions = 0
        rng = random.Random(918273)
        with (args.output/'results.jsonl').open('w') as output:
            for index, layout in enumerate(cases):
                for field in fields(layout):
                    expected = runner.expected(field)
                    scratch = {name:{} for name in runner.kernels}
                    for repeat in range(-1,protocol['warm_repetitions']):
                        order = list(runner.kernels); rng.shuffle(order)
                        for name in order:
                            records += 1
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
                            executions += 1
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
    finish_run(args.output,freeze,records,executions)


if __name__ == '__main__':
    main()
