#!/usr/bin/env python3
"""Assign auxiliary checks together with the exact native job listing."""
import argparse
import json
import os
from pathlib import Path
from types import SimpleNamespace
import run_tests


def plan(jobs, auxiliary, count, timings):
    if timings is None:
        return {value['id']: value['default_shard'] for value in auxiliary.values()}
    chosen = run_tests.assignments([job.label for job in jobs] + list(auxiliary), count, timings)
    return {value['id']: chosen[key] for key, value in auxiliary.items()}


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--timing-profile',type=Path,required=True)
    p.add_argument('--auxiliary-jobs',type=Path,required=True)
    p.add_argument('--count',type=int,required=True)
    a=p.parse_args()
    if a.count < 1: p.error('count must be positive')
    args=SimpleNamespace(in_process=False,no_display=False,quick=False,filter=[],tag=[],exclude_tag=['map-generators'])
    cases=run_tests.list_cases(run_tests.binary_path(a.build_dir,'engine'),'engine')
    kept,_=run_tests.select(cases,args)
    result=plan(run_tests.make_jobs(kept,args,cases),json.loads(a.auxiliary_jobs.read_text()),a.count,run_tests.load_timings(a.timing_profile))
    output=''.join(f'{key}={value}\n' for key,value in result.items())
    print(output)
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'],'a') as target:target.write(output)
if __name__=='__main__':main()
