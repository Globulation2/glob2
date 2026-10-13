#!/usr/bin/env python3
"""Analyze complete final renderer evidence. Never infer latency from profiled runs."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import random
import re
import statistics as st

VARIANTS = ('baseline-immediate', 'baseline-parallel', 'candidate-immediate',
            'candidate-serial', 'candidate-parallel')
FIXED = ('static-map', 'animated-map', 'half-gui', 'native-gui', 'streamed-map')
NATIVE = ('static-warm', 'animated-warm', 'gui-half-pan', 'gui-native-pan', 'streamed-pages')
RECORD = ('animated-warm', 'gui-half-pan', 'gui-native-pan', 'streamed-pages')
CG_VARIANTS = ('baseline-immediate', 'candidate-immediate', 'candidate-parallel')
EVENTS = ('instructions:u', 'instructions:k', 'branches:u', 'branches:k')
PAIRINGS = (('new_parallel_vs_new_immediate', 'candidate-parallel', 'candidate-immediate'),
            ('new_parallel_vs_old_immediate', 'candidate-parallel', 'baseline-immediate'),
            ('new_immediate_vs_old_immediate', 'candidate-immediate', 'baseline-immediate'),
            ('new_parallel_vs_old_parallel', 'candidate-parallel', 'baseline-parallel'))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(path.read_text())


def summary(values):
    require(bool(values), 'empty summary')
    return {'median': st.median(values), 'mean': st.mean(values), 'min': min(values),
            'max': max(values), 'values': values}


def paired_ratio(numerator, denominator, label):
    require(len(numerator) == len(denominator) == 12, f'{label}: expected 12 independent pairs')
    ratios = [a / b for a, b in zip(numerator, denominator)]
    require(all(x > 0 for x in ratios), f'{label}: nonpositive measurements')
    # Sampling unit is one independent paired run, never individual frames.
    rng = random.Random(int.from_bytes(hashlib.sha256(label.encode()).digest()[:8], 'big'))
    bootstrap = sorted(st.median(rng.choices(ratios, k=12)) for _ in range(20000))
    return {'ratio': st.median(ratios), 'ci95': [bootstrap[499], bootstrap[19499]],
            'paired_ratios': ratios, 'numerator_ms': numerator, 'denominator_ms': denominator,
            'estimator': 'median of paired per-run median ratios; percentile paired bootstrap, 20000 resamples'}


def validate_audit(path):
    audit = load(path)
    require(audit.get('state') == 'completed' and audit.get('exit_code') == 0,
            'cpuset command did not complete successfully')
    require(audit.get('dry_run') is False and audit.get('cgroup_restored') is True and
            audit.get('wrapper_affinity_restored') is True and not audit.get('forced_cgroup_kill') and
            not audit.get('cleanup_error'), 'cpuset audit is dry-run, incomplete or unrestored')
    require(audit.get('validity_checks', 0) > 0, 'cpuset audit lacks continuous validity checks')
    for field in ('partition_before_command', 'partition_after_command', 'last_partition_check'):
        require(audit.get(field, {}).get('cpuset.cpus.partition') == 'root', f'invalid audit {field}')
    return audit


def timing(root):
    folder = root / 'comparison'
    audit = validate_audit(folder / 'cpuset-audit.json')
    manifest = load(folder / 'manifest.json')
    require(manifest['affinity'] == audit['preflight']['engine_cpus'], 'timing affinity differs from audit')
    expected = {f'{case}-{r:02d}-{v}' for case in (*FIXED, 'live-simulation') for r in range(12) for v in VARIANTS}
    entries = {entry['label']: entry for entry in manifest['runs']}
    require(len(entries) == len(manifest['runs']) and set(entries) == expected, 'timing matrix is incomplete or duplicated')
    durations = {}; live = {}; observations = {}; captures = {}
    for label, entry in entries.items():
        require(entry.get('validated') is True and entry.get('returncode') == 0,
                f'{label}: unvalidated or failed timing run')
        log = (folder / f'{label}.log').read_text()
        require('timing_valid=true' in log and 'timing_valid=false' not in log and
                'PERF_BOUNDARY' not in log and 'PROFILE_BOUNDARY phase=' not in log,
                f'{label}: instrumented or unspecified timing validity')
        require(not any(k in ('LD_PRELOAD', 'LD_AUDIT', 'DYLD_INSERT_LIBRARIES') or
                        k.startswith('GLOB2_PROFILE_') for k in entry['environment']),
                f'{label}: interposition environment')
        case = next(c for c in (*FIXED, 'live-simulation') if label.startswith(c + '-'))
        variant = next(v for v in VARIANTS if label.endswith('-' + v))
        repeat = int(label[len(case)+1:len(case)+3])
        if case == 'live-simulation':
            obs = entry['observation']
            require(obs['kind'] == 'live_progress_and_latency' and obs['final_tick'] > obs['initial_tick'] and
                    obs['frames'] > 0, f'{label}: live probe did not progress')
            live[(variant, repeat)] = obs
            continue
        obs = entry['observation']
        require(obs['kind'] == 'fixed_work', f'{label}: not fixed work')
        require(case not in observations or observations[case] == obs, f'{label}: workload changed')
        observations[case] = obs
        for metric in ('draw', 'frame'):
            match = re.search(rf'^{metric} mean_ms=([0-9.]+) median_ms=([0-9.]+) p95_ms=([0-9.]+) p99_ms=([0-9.]+)$', log, re.M)
            require(match is not None and float(match[2]) > 0, f'{label}: missing/zero {metric}')
            durations[(case, variant, repeat, metric)] = float(match[2])
        if repeat == 0:
            bmp = folder / f'{label}.bmp'
            actual = hashlib.sha256(bmp.read_bytes()).hexdigest()
            require(actual == entry['capture_sha256'], f'{label}: capture changed')
            if variant.startswith('candidate-'):
                require(case not in captures or captures[case] == actual, f'{label}: candidate capture mismatch')
                captures[case] = actual
    results = {}
    for case in FIXED:
        results[case] = {}
        for metric in ('draw', 'frame'):
            results[case][metric] = {name: paired_ratio(
                [durations[(case, a, r, metric)] for r in range(12)],
                [durations[(case, b, r, metric)] for r in range(12)], f'{case}/{metric}/{name}')
                for name, a, b in PAIRINGS}
    live_results = {}
    for variant in VARIANTS:
        runs = [live[(variant, r)] for r in range(12)]
        live_results[variant] = {'runs': runs, 'tick_progress': summary([x['final_tick']-x['initial_tick'] for x in runs]),
            'displayed_frames': summary([x['frames'] for x in runs]),
            'missed_deadlines': summary([x['missed_render_deadlines'] for x in runs]),
            'latency': {metric: {quantile: summary([x['latency'][metric][quantile] for x in runs])
                       for quantile in ('mean_ms', 'median_ms', 'p95_ms', 'p99_ms')}
                       for metric in ('draw_and_present', 'displayed_scene_age', 'last_simulation_deadline_lateness')}}
    return {'fixed': results, 'live': live_results, 'workloads': observations,
            'binary_and_fixture_hashes': manifest['sha256'], 'candidate_capture_hashes': captures,
            'cpuset_audit': str(folder / 'cpuset-audit.json'),
            'limits': audit['limits'], 'draw_scope': 'Draw includes gui.prepareLocalPresentation scene/snapshot capture every frame, command recording and intermediate flushes. Simulation state is fixed/paused where configured; one extracted scene is not reused throughout. Final raster work may occur during presentation. Complete frame ratios are required alongside draw ratios.', 'live_scope': 'Live duration probes have differing work; no fixed-work ratio inferred.'}


def native(root):
    folder = root / 'perf'
    expected_stat = {f'{c}-{r}-{v}-stat' for c in NATIVE for r in range(3) for v in VARIANTS}
    expected_record = {f'{c}-0-{v}-record' for c in RECORD for v in CG_VARIANTS}
    available = {p.parent.name: p for p in folder.glob('*/metadata.json')}
    require(set(available) == expected_stat | expected_record, 'native matrix incomplete or contains extra profiles')
    counts = {}; hashes = {}; profiles = {}
    for label, path in available.items():
        meta = load(path); case, variant, repeat = meta['case'], meta['variant'], meta['repeat']
        expected_hash = hashes.setdefault(variant, meta['binary_sha256'])
        require(expected_hash == meta['binary_sha256'], f'{label}: binary changed')
        log = (path.parent / 'run.log').read_text()
        require(log.count('PERF_BOUNDARY ') == 2 and 'simulation_checksum=2231453127' in log,
                f'{label}: invalid profiler boundaries/checksum')
        frames = int(meta['environment']['PROFILE_FRAMES'])
        require(frames == (5 if case == 'streamed-pages' else 10), f'{label}: wrong fixed frame count')
        if meta['kind'] == 'stat':
            rows = [row for row in csv.reader((path.parent/'stat.csv').read_text().splitlines())
                    if row and not row[0].startswith('#')]
            require(len(rows) == 4 and {row[2] for row in rows} == set(EVENTS), f'{label}: wrong counters')
            require(all(row[0].isdigit() and float(row[4]) >= 99.99 for row in rows),
                    f'{label}: missing/multiplexed counters')
            counts[(case, variant, repeat)] = {row[2]: int(row[0])/frames for row in rows}
        else:
            report = (path.parent/'self.txt').read_text()
            lost = re.search(r'Total Lost Samples:\s*(\d+)', report)
            require(lost is not None, f'{label}: no sample-loss accounting')
            sample_lines = (path.parent/'samples.txt').read_text().splitlines()
            headers = [i for i,line in enumerate(sample_lines) if re.search(r'\d+/\d+\s+[0-9.]+: instructions:u:', line)]
            leaf = []
            for j, start in enumerate(headers):
                end = headers[j+1] if j+1 < len(headers) else len(sample_lines)
                stack = [line.strip() for line in sample_lines[start+1:end]
                         if re.match(r'^\s+[0-9a-f]+\s', line)]
                if stack:
                    leaf.append(stack[0])
            unresolved = sum('[unknown]' in x or bool(re.match(r'^[0-9a-f]+\s+0x[0-9a-f]+(?:\s|$)', x)) for x in leaf)
            profiles[label] = {'lost_samples': int(lost[1]), 'total_sample_headers': len(headers),
                               'parsed_leaf_samples': len(leaf), 'missing_stack_samples': len(headers)-len(leaf),
                               'unresolved_leaf_samples': unresolved,
                               'scope': 'Instruction-period samples; exclusive leaf attribution, not CPU time. Missing stacks limit worker attribution; exact worker instruction coverage comes from Callgrind.'}
    aggregate = {case: {variant: {event: summary([counts[(case,variant,r)][event] for r in range(3)])
                 for event in EVENTS} for variant in VARIANTS} for case in NATIVE}
    return {'per_frame': aggregate, 'sample_quality': profiles, 'binary_hashes': hashes,
            'scope': 'Three independent repetitions; user/kernel instructions and branches separated. No latency or bandwidth claim.'}


def callgrind(root):
    parsed = load(root/'callgrind-parsed.json'); runs = parsed['runs']
    expected = {('gui-half-pan', v, r) for v in CG_VARIANTS for r in range(3)}
    require(len(runs) == 9 and {(x['case'],x['variant'],x['repeat']) for x in runs} == expected,
            'Callgrind GUI matrix incomplete or duplicated')
    out = {}
    for variant in CG_VARIANTS:
        selected = sorted((x for x in runs if x['variant'] == variant), key=lambda x:x['repeat'])
        for run in selected:
            require(run['frames'] == 10 and run.get('validation', '').startswith('Every thread self-cost sum'),
                    'Callgrind lacks reconciled fixed-frame totals')
            require(sum(x['totals']['Ir'] for x in run['threads']) == run['totals']['Ir'], 'Callgrind thread sum mismatch')
        worker_share = [sum(t['totals']['Ir'] for t in x['threads'] if t['thread'] != 1 and t['has_graphics_work']) / x['totals']['Ir'] for x in selected]
        out[variant] = {'instructions_per_frame': summary([x['per_frame']['Ir'] for x in selected]),
            'branches_per_frame': summary([x['per_frame']['Bc']+x['per_frame']['Bi'] for x in selected]),
            'graphics_worker_instruction_fraction': summary(worker_share)}
    return {'half_gui': out, 'scope': 'Exact reconciled instruction/branch work. Threads serialized by Callgrind; worker instruction fraction is not CPU time.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    # Do not write a partial result: all required final datasets must validate.
    result = {'timing': timing(root), 'native': native(root), 'callgrind': callgrind(root),
              'method': 'Timing uses independent paired run medians; profiler timing excluded. Instruction evidence analyzed separately.'}
    half = result['timing']['fixed']['half-gui']
    static = result['timing']['fixed']['static-map']['frame']['new_parallel_vs_old_immediate']
    pmu = result['native']['per_frame']['gui-half-pan']
    def total_instructions(variant):
        return st.median([u+k for u,k in zip(pmu[variant]['instructions:u']['values'], pmu[variant]['instructions:k']['values'])])
    overhead = total_instructions('candidate-parallel') / total_instructions('candidate-immediate')
    worker = result['callgrind']['half_gui']['candidate-parallel']['graphics_worker_instruction_fraction']['median']
    result['acceptance_observations'] = {
        'half_gui_parallel_draw_ratio': half['draw']['new_parallel_vs_new_immediate'],
        'half_gui_draw_10percent_target_met': half['draw']['new_parallel_vs_new_immediate']['ratio'] <= .9 and half['draw']['new_parallel_vs_new_immediate']['ci95'][1] < 1,
        'half_gui_complete_frame_vs_new_immediate': half['frame']['new_parallel_vs_new_immediate'],
        'half_gui_complete_frame_10percent_target_met': half['frame']['new_parallel_vs_new_immediate']['ratio'] <= .9 and half['frame']['new_parallel_vs_new_immediate']['ci95'][1] < 1,
        'half_gui_complete_frame_vs_old': half['frame']['new_parallel_vs_old_immediate'],
        'half_gui_complete_frame_improvement_established': half['frame']['new_parallel_vs_old_immediate']['ci95'][1] < 1,
        'static_frame_vs_old': static,
        'static_5percent_regression_bound_established': static['ci95'][1] <= 1.05,
        'half_gui_user_plus_kernel_instruction_ratio': overhead,
        'half_gui_instruction_overhead_10percent_target_met': overhead <= 1.1,
        'half_gui_graphics_worker_instruction_fraction': worker,
        'half_gui_worker_60percent_target_met': worker >= .6,
        'limits': 'Medians of paired per-frame user-plus-kernel counts have three repeats; kernel cost varies. Numerical targets are not automatic activation or platform-wide approval.'}
    output = args.output or root/'final-analysis.json'
    output.write_text(json.dumps(result, indent=2)+'\n')
    print('Fixed-work frame-median ratios (lower is better), paired 95% CI')
    print('case | new parallel/new immediate | new parallel/old immediate')
    for case, data in result['timing']['fixed'].items():
        def cell(name):
            x=data['frame'][name]
            return f"{x['ratio']:.4f} [{x['ci95'][0]:.4f}, {x['ci95'][1]:.4f}]"
        print(case, '|', cell('new_parallel_vs_new_immediate'), '|', cell('new_parallel_vs_old_immediate'))
    print('Native million instructions/frame: old immediate | new immediate | new parallel (user; kernel)')
    for case, data in result['native']['per_frame'].items():
        values = [f"{data[v]['instructions:u']['median']/1e6:.3f}; {data[v]['instructions:k']['median']/1e6:.3f}" for v in CG_VARIANTS]
        print(case, '|', ' | '.join(values))
    print('Full validated results:', output)


if __name__ == '__main__':
    main()
