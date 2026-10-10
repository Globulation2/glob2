"""Paired whole-process CPU evidence; no timing threshold is a CI assertion."""
import math
import random
import statistics


def paired_interval(ratios, *, draws=5000, seed=1045):
    """Bootstrap independent paired rounds, on the log scale, without trimming."""
    if len(ratios) < 2 or any(not math.isfinite(v) or v <= 0 for v in ratios):
        raise ValueError('at least two positive finite paired ratios required')
    logs = [math.log(v) for v in ratios]
    rng = random.Random(seed)
    means = sorted(math.exp(statistics.mean(rng.choices(logs, k=len(logs)))) for _ in range(draws))
    return {'ratio': math.exp(statistics.mean(logs)),
            'lower95': means[int(.025 * (draws - 1))],
            'upper95': means[int(.975 * (draws - 1))],
            'upper_one_sided95': means[int(.95 * (draws - 1))],
            'pairs': len(ratios)}


def measurement(result):
    ticks = result['benchmark_measured_ticks']
    if ticks <= 0:
        raise ValueError('empty measured window')
    return {'cpu_per_tick': result['benchmark_run_cpu_ns'] / ticks,
            'wall_per_tick': result['benchmark_run_wall_ns'] / ticks,
            'tick_p99': result['tick_p99_ns'],
            'publication_wait_per_tick': result['benchmark_publication_wait_ns'] / ticks}


def cpu_ceiling(result):
    """Ideal CPU-removal ceiling for observed exact required-work diagnostics.

    This is an upper bound before GPU host work, transfers and classification,
    never a promised improvement. Existing elapsed worker timings are excluded.
    """
    total = result.get('benchmark_run_cpu_ns', 0)
    start, end = result.get('benchmark_gradient_at_start', {}), result.get('benchmark_gradient_at_end', {})
    names = ('required_seed_cpu_ns', 'required_propagation_cpu_ns')
    if not start.get('thread_cpu_clock_available') or not end.get('thread_cpu_clock_available'):
        return {'available': False, 'reason': 'thread CPU clock availability not established'}
    if not start.get('cpu_diagnostics_enabled') or not end.get('cpu_diagnostics_enabled'):
        return {'available': False, 'reason': 'CPU diagnostics availability not established'}
    if total <= 0 or any(name not in start or name not in end for name in names):
        return {'available': False, 'reason': 'exact required-work CPU counters unavailable'}
    if end.get('gpu_complete_fields', 0) != start.get('gpu_complete_fields', 0):
        return {'available': False, 'reason': 'CPU-only reference required for an offload ceiling'}
    delta = {name: end[name] - start[name] for name in names}
    if any(v < 0 for v in delta.values()) or sum(delta.values()) > total:
        return {'available': False, 'reason': 'counter interval or overlap mismatch'}
    fraction = sum(delta.values()) / total
    return {'available': True, 'process_cpu_ns': total, **delta,
            'ideal_cpu_removal_fraction': fraction,
            'periodic_only_can_reach_30_percent': fraction >= .30,
            'note': 'before accelerator host work; expand measured eligibility when this ceiling is below target'}


def noninferiority(pairs, limit, *, draws=5000, seed=1045):
    """Bound paired excess C-limit*B; handles exact zero waits without pseudo-counts."""
    values = [c - limit * b for b, c in pairs]
    rng = random.Random(seed)
    means = sorted(statistics.mean(rng.choices(values, k=len(values))) for _ in range(draws))
    upper = means[int(.95 * (draws - 1))]
    return {'upper_excess_ns': upper, 'pass': upper <= 0,
            'point_nonworsening': statistics.mean(c-b for b, c in pairs) <= 0}


def summarize(rows, control, candidates, *, minimum_pairs=5, confirmation=False):
    """Analyze each scenario separately; rounds never become independent maps."""
    output = {}
    for scenario in sorted({r['scenario'] for r in rows}):
        selected = [r for r in rows if r['scenario'] == scenario and r['round'] >= 0]
        pairs = {(r['round'], r['variant']): r for r in selected}
        if len(pairs) != len(selected):
            raise ValueError('duplicate paired observation')
        scenario_result = {}
        for candidate in candidates:
            rounds = sorted({r['round'] for r in selected})
            if any((n, v) not in pairs for n in rounds for v in (control, candidate)):
                scenario_result[candidate] = {'qualified': False, 'reason': 'missing paired sample'}
                continue
            if len(rounds) < minimum_pairs:
                scenario_result[candidate] = {'qualified': False, 'reason': 'insufficient paired rounds'}
                continue
            matched = [(pairs[n, control], pairs[n, candidate]) for n in rounds]
            if any(not b.get('valid', True) or not c.get('valid', True) for b, c in matched):
                scenario_result[candidate] = {'qualified': False, 'reason': 'invalid workload or GPU execution'}
                continue
            metrics = {}
            for key in measurement(matched[0][0]['result']):
                values = [(measurement(b['result'])[key], measurement(c['result'])[key]) for b, c in matched]
                metrics[key] = paired_interval([c / b for b, c in values]) if all(b > 0 and c > 0 for b, c in values) else {'ratio': None, 'pairs': len(rounds), 'note': 'zero values: absolute paired guard'}
                limit = 1.02 if key == 'wall_per_tick' else 1.05
                if confirmation and key == 'publication_wait_per_tick': limit = 1.
                metrics[key]['noninferiority'] = noninferiority(values, limit)
            cpu = metrics['cpu_per_tick']
            passing = all(metrics[k]['noninferiority']['pass'] for k in ('wall_per_tick', 'tick_p99', 'publication_wait_per_tick'))
            if confirmation:
                passing = passing and all(metrics[k]['noninferiority']['point_nonworsening'] for k in ('wall_per_tick', 'tick_p99'))
            contaminated = any(r.get('resource_contaminated', False) for pair in matched for r in pair)
            scenario_result[candidate] = {'metrics': metrics, 'noninferiority_pass': passing,
                'resource_contaminated': contaminated,
                'cpu_target_pass': cpu.get('upper_one_sided95', math.inf) <= .70,
                'qualified': not contaminated and passing and cpu.get('upper_one_sided95', math.inf) <= .70,
                'scope': 'this retained scenario; independent holdouts and rendered guards required separately'}
        output[scenario] = scenario_result
    return output


def aggregate_cpu(rows, control, candidates, *, required_phases=('early', 'middle', 'late'), draws=5000):
    """Equal-stratum CPU ratios, clustering phases/rounds inside independent maps."""
    output = {}
    selected = [r for r in rows if r['round'] >= 0 and not r.get('control', False)]
    if any(not r.get('map_id') or not r.get('group') for r in selected):
        return {'available': False, 'reason': 'independent map and stratum identities required'}
    for candidate in candidates:
        pair_index = {(r['scenario'], r['round'], r['variant']): r for r in selected}
        maps = {}
        errors = []
        for scenario, n in sorted({(r['scenario'], r['round']) for r in selected}):
            b, c = pair_index.get((scenario, n, control)), pair_index.get((scenario, n, candidate))
            if not b or not c or not b.get('valid', True) or not c.get('valid', True):
                errors.append('missing or invalid paired sample'); continue
            if b.get('resource_contaminated', False) or c.get('resource_contaminated', False):
                errors.append('resource contamination prevents acceptance'); continue
            m = maps.setdefault(b['map_id'], {'stratum': b['group'], 'phases': set(), 'logs': []})
            if m['stratum'] != b['group']: raise ValueError('map appears in multiple strata')
            m['phases'].add(b.get('phase'))
            m['logs'].append(math.log(measurement(c['result'])['cpu_per_tick'] / measurement(b['result'])['cpu_per_tick']))
        if any(set(required_phases) != m['phases'] for m in maps.values()):
            errors.append('required phases missing; absent phases cannot be treated as wins')
        strata = {}
        for m in maps.values(): strata.setdefault(m['stratum'], []).append(statistics.mean(m['logs']))
        if not strata or any(len(values) < 2 for values in strata.values()):
            errors.append('at least two independent maps per stratum required for development estimate')
        if errors:
            output[candidate] = {'available': False, 'errors': sorted(set(errors))}; continue
        rng = random.Random(1045)
        boot = sorted(math.exp(statistics.mean(statistics.mean(rng.choices(v, k=len(v))) for v in strata.values())) for _ in range(draws))
        upper = boot[int(.95 * (draws - 1))]
        output[candidate] = {'available': True,
            'ratio': math.exp(statistics.mean(statistics.mean(v) for v in strata.values())),
            'upper_one_sided95': upper, 'cpu_target_pass': upper <= .7,
            'independent_maps': len(maps), 'maps_per_stratum': {g: len(v) for g, v in strata.items()},
            'note': 'equal strata; phases and repetitions clustered by independent map; held-out confirmation required'}
    return output
