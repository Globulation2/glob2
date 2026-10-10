#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed, map-level screening. Offline success alone never admits a plan."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics

from corpus import digest, manifest, qualifying_class
from contracts import CANDIDATES, CONFIGS, validate_completed


def median(values):
    return statistics.median(values)


def mad(values):
    center = median(values)
    return median([abs(x-center) for x in values])


def wins(candidate, baseline, protocol):
    c = [r['total_ns'] for r in candidate if r['repeat'] >= 0]
    b = [r['total_ns'] for r in baseline if r['repeat'] >= 0]
    advantage = median(b)-median(c)
    noise = protocol['noise_multiplier_mad'] * max(mad(c),mad(b))
    threshold = max(protocol['minimum_absolute_advantage_ns'],
                    protocol['minimum_relative_advantage']*median(b),noise)
    cold_c = next(r['total_ns'] for r in candidate if r['repeat'] == -1)
    cold_b = next(r['total_ns'] for r in baseline if r['repeat'] == -1)
    cold_threshold = max(protocol['minimum_absolute_advantage_ns'],
                         protocol['minimum_relative_advantage']*cold_b,noise)
    return dict(win=advantage>threshold and cold_b-cold_c>cold_threshold,
                warm_advantage_fraction=advantage/median(b),
                cold_advantage_fraction=(cold_b-cold_c)/cold_b,
                noise_ns=noise,required_advantage_ns=threshold)


def analyze(directory):
    freeze, receipt = validate_completed(directory)
    protocol = freeze['protocol']; cases = freeze['corpus']
    expected = {(layout['id'],stage,plan,repeat)
                for layout in cases for stage in range(3)
                for plan in (*CONFIGS,*CANDIDATES)
                for repeat in range(-1,protocol['warm_repetitions'])}
    groups = defaultdict(list); count=0; executions=0; dimensions=defaultdict(int)
    layouts = {case['id']:case for case in cases}
    inputs = {}
    for line in (directory/'results.jsonl').read_text().splitlines():
        row = json.loads(line)
        key = (row['layout']['id'],row['stage'],row['plan'],row['repeat'])
        if key not in expected:
            raise ValueError('unexpected or duplicate sample')
        expected.remove(key)
        if row['layout'] != layouts[key[0]]:
            raise ValueError('layout identity changed')
        context = tuple(row.get(k) for k in ('cap','seed_count','cost_classes','minimum_step','obstacle_count','input_sha256'))
        if inputs.setdefault(key[:2],context) != context:
            raise ValueError('plans or repetitions received different inputs')
        metadata = dict(row, width=row['layout']['width'],height=row['layout']['height'])
        class_match = qualifying_class(row['plan'],metadata) if row['plan'] in CANDIDATES else False
        eligible = row['plan'] != 'bounded' or row['cap'] <= 16*row['minimum_step']
        if row['class_match'] != class_match or row.get('eligible',True) != eligible:
            raise ValueError('incorrect workload classification or semantic exclusion')
        if eligible and (type(row.get('total_ns')) is not int or row['total_ns'] <= 0):
            raise ValueError('invalid execution timing')
        if row.get('eligible', True) and (row.get('exact') is not True or row.get('error')):
            raise ValueError('oracle/device failure')
        groups[key[:3]].append(row);count+=1
        executions += int(eligible)
    if expected:
        raise ValueError('missing samples')
    if receipt.get('records') != count or receipt.get('executions') != executions:
        raise ValueError('completion counts differ from observed results')
    for layout in cases:
        dimensions[f"{layout['width']}x{layout['height']}"] += 1
    comparisons = {}; summary={}
    for candidate in CANDIDATES:
        maps = defaultdict(list)
        for layout in cases:
            for stage in range(3):
                rows = groups[(layout['id'],stage,candidate)]
                if not all(r['class_match'] for r in rows):
                    continue
                # Must beat every retained implementation, not a chosen weak baseline.
                results = {name:wins(rows,groups[(layout['id'],stage,name)],protocol)
                           for name in CONFIGS}
                maps[layout['id']].append(dict(stage=stage,
                    win=all(r['win'] for r in results.values()),versus=results))
        passed = sum(all(f['win'] for f in values) for values in maps.values())
        total = len(maps); fraction=passed/total if total else 0
        required = protocol['minimum_held_out_class_maps'] if freeze['split']=='final' else 1
        summary[candidate] = dict(independent_class_maps=total,winning_maps=passed,
             win_fraction=fraction,minimum_maps=required,
             passes_screen=total>=required and fraction>=protocol['minimum_win_fraction'])
        comparisons[candidate]=dict(maps)
    survivors = [name for name in freeze['candidates'] if summary[name]['passes_screen']]
    final_coverage = (freeze['split']=='final' and len(cases)>=protocol['final_layouts'] and
                      all(dimensions.get(f'{size}x{size}',0)>=protocol['layouts_per_square_size'] for size in protocol['sizes']))
    return dict(split=freeze['split'],sources=freeze['sources'],corpus_sha256=freeze['corpus_sha256'],
                layouts=len(cases),records=count,executions=executions,semantic_skips=count-executions,
                dimensions=dict(dimensions),exact=True,
                eligible_candidates=freeze['candidates'],screen_survivors=survivors,summary=summary,comparisons=comparisons,
                kernel_qualified=survivors if final_coverage else [],admitted=[],
                reason='Offline timings cannot establish selectability, competitive coverage or integrated benefit; no production admission')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    report=analyze(args.directory)
    (args.directory/'analysis.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items() if k not in ('sources','comparisons')},indent=2))


if __name__=='__main__':
    main()
