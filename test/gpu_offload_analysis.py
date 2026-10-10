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
    if total <= 0 or any(name not in start or name not in end for name in names):
        return {'available': False, 'reason': 'exact required-work CPU counters unavailable'}
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
    return {'upper_excess_ns': upper, 'pass': upper <= 0}


def summarize(rows, control, candidates, *, minimum_pairs=5):
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
                metrics[key]['noninferiority'] = noninferiority(values, 1.02 if key == 'wall_per_tick' else 1.05)
            cpu = metrics['cpu_per_tick']
            passing = all(metrics[k]['noninferiority']['pass'] for k in ('wall_per_tick', 'tick_p99', 'publication_wait_per_tick'))
            scenario_result[candidate] = {'metrics': metrics, 'noninferiority_pass': passing,
                'cpu_target_pass': cpu.get('upper_one_sided95', math.inf) <= .70,
                'qualified': passing and cpu.get('upper_one_sided95', math.inf) <= .70,
                'scope': 'this retained scenario; independent holdouts and rendered guards required separately'}
        output[scenario] = scenario_result
    return output
