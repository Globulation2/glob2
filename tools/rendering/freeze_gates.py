#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze renderer gates from repeated original release runs before optimization.

The output is ignored evidence, not a claim of a new backend's acceptance.
Use a same-executable software A/A comparison to measure paired CPU noise.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def gate(values):
    if len(values) < 7 or min(values) <= 0:
        raise ValueError('Each gate requires seven positive release measurements')
    median = statistics.median(values)
    noise = statistics.median(abs(value-median)/median for value in values)
    return {'samples': values, 'baseline_median': median, 'relative_median_absolute_deviation': noise,
            'maximum_relative_regression': .05, 'baseline_repeatability_good':noise<=.05,
            'minimum_relative_improvement_to_claim_benefit': max(.05, 3*noise)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, action='append', required=True, help='collect_baseline baseline.json')
    parser.add_argument('--software-aa', type=Path, help='same executable software benchmark results.json')
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    report={'inputs':[], 'performance':[], 'visual':{
        'cpu_terrain_and_integer_gpu_composition':'Exact RGBA bytes, including custom alpha, seams and box reduction.',
        'deterministic_2d_primitives':'Exact interior pixels; predeclared primitive edge coverage fixtures.',
        'skin_same_device':'Exact alpha coverage, interior RGB p99 <= 1/255; procedural derivative outliers reviewed with paired images.',
        'driver_dependent_3d':'Compare paired images and declare driver-specific tolerances before acceptance; no blanket maximum-channel waiver.'},
        'resource_limits_mib':{'general_textures':64,'skin_atlas':64,'terrain_pages':128,'terrain_masks':32,'combined_gpu':256},
        'queue_limits':{'commands':4096,'vertices':65536,'draw_submissions':8,'reserved_completion_submissions':1,'gpu_captures':3},
        'limitations':['Defaults remain opt-in until all affected features and accepted hardware evidence pass.',
                       'Startup process wall time includes asset loading and rendering; it is not isolated cold disk timing.',
                       'Missing workloads/platforms require additional baseline gates before their optimization/default changes.',
                       'A noisy baseline cannot justify a wider regression ceiling; refine and repeat it before acceptance.']}
    for path in args.baseline:
        data=json.loads(path.read_text())
        if len(data['runs'])<7 or any(run['exit_code'] for run in data['runs']):
            raise ValueError('Need seven successful release baseline runs')
        report['inputs'].append({'path':str(path.resolve()),'sha256':digest(path),'binary':data.get('binary'),'base':data['base'],'revision':data['revision'],'build_command':data['build_command']})
        metrics={}
        for run in data['runs']:
            text=Path(run['log']).read_text()
            for label, name, value in re.findall(r'^(.*?) (median|p95|p99|process_cpu_median|process_cpu_p95)=([0-9.]+) ms',text,re.M):
                metrics.setdefault(label+'/'+name,[]).append(float(value))
            # The p95 shares the median line in the production harness.
            labels=[]
            for label,value in re.findall(r'^(.*?) median=[0-9.]+ ms p95=([0-9.]+) ms',text,re.M):
                labels.append(label)
                metrics.setdefault(label+'/p95',[]).append(float(value))
            for label,value in re.findall(r'^(.*?) process_cpu_median=[0-9.]+ ms process_cpu_p95=([0-9.]+) ms',text,re.M):
                metrics.setdefault(label+'/process_cpu_p95',[]).append(float(value))
            cold=re.findall(r'^WARMUP frame=0 elapsed_ms=([0-9.]+) process_cpu_ms=([0-9.]+)',text,re.M)
            if len(cold)==len(labels):
                for label,(wall,cpu) in zip(labels,cold):
                    metrics.setdefault(label+'/first_draw_elapsed',[]).append(float(wall))
                    metrics.setdefault(label+'/first_draw_process_cpu',[]).append(float(cpu))
        if not metrics: raise ValueError('Missing production benchmark metrics')
        for name,values in metrics.items():
            report['performance'].append({'workload':data.get('workload'),'metric':name,**gate(values)})
    if args.software_aa:
        data=json.loads(args.software_aa.read_text()); params=data['arguments']
        if Path(params['baseline']).resolve()!=Path(params['candidate']).resolve():
            raise ValueError('Software noise requires a same executable A/A comparison')
        report['inputs'].append({'path':str(args.software_aa.resolve()),'sha256':digest(args.software_aa)})
        for row in data['summary']:
            before,after=row['cpu_runs']['baseline'],row['cpu_runs']['candidate']
            if len(before)<7 or len(before)!=len(after): raise ValueError('Need seven interleaved software A/A pairs')
            noise=statistics.median(abs(a-b)/a for a,b in zip(before,after))
            report['performance'].append({'workload':'software/'+row['scenario'],'metric':'process_cpu_ms_per_frame',**gate(before),
                'paired_relative_noise':noise,'baseline_repeatability_good':noise<=.05,'maximum_relative_regression':.05,
                'minimum_relative_improvement_to_claim_benefit':max(.05,3*noise)})
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(args.output)

if __name__=='__main__': main()
