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
    """Observed completed-job CPU removal estimate, not an exact window ceiling.

    Phase counters report full job lifetimes at completion. Jobs straddling the
    endpoints are not clipped to the process-CPU window, so these deltas cannot
    establish a bound or the achievable saving after accelerator host work.
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
            'observed_cpu_removal_fraction': fraction,
            'observed_fraction_at_least_30_percent': fraction >= .30,
            'exact_window_ceiling_established': False, 'qualifying_evidence': False,
            'counter_semantics': 'completed job lifetime deltas; not time intersection',
            'note': 'approximate removal estimate before accelerator host work; boundary-straddling jobs prevent an exact ceiling'}


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
            diagnostic = any(r.get('diagnostic_stage', False) or r['result'].get('benchmark_diagnostics_enabled', False) for pair in matched for r in pair)
            scenario_result[candidate] = {'metrics': metrics, 'noninferiority_pass': passing,
                'diagnostic_only': diagnostic,
                'resource_contaminated': contaminated,
                'cpu_target_pass': cpu.get('upper_one_sided95', math.inf) <= .70,
                'qualified': not contaminated and not diagnostic and passing and cpu.get('upper_one_sided95', math.inf) <= .70,
                'scope': 'this retained scenario; independent holdouts and rendered guards required separately'}
        output[scenario] = scenario_result
    return output


def aggregate_cpu(rows, control, candidates, *, expected_scenarios=None,
                  expected_rounds=None, minimum_pairs=5,
                  required_phases=('early', 'middle', 'late'), draws=5000):
    """Validate declared coverage, then cluster equal phase means by whole map.

    Small-map control scenarios are checked against the complete protocol but
    excluded from the primary large-map average only when declared as controls.
    The roster must be independent of available rows, never inferred from them.
    """
    def unavailable(*errors):
        return {c: {'available': False, 'errors': sorted(set(errors))} for c in candidates}
    if not expected_scenarios or expected_rounds is None:
        return unavailable('explicit expected scenario roster and paired rounds required')
    rounds = list(expected_rounds)
    if (minimum_pairs < 5 or len(rounds) < minimum_pairs or len(set(rounds)) != len(rounds)
            or any(type(n) is not int or n < 0 for n in rounds)):
        return unavailable('invalid or insufficient declared paired rounds')
    variants = [control, *candidates]
    if len(variants) != len(set(variants)):
        return unavailable('distinct control and candidate identities required')
    roster = {s['id']: s for s in expected_scenarios}
    if len(roster) != len(expected_scenarios):
        return unavailable('duplicate expected scenario identity')
    names = ('map_id', 'group', 'phase')
    if any(any(not s.get(k) for k in names) for s in roster.values()):
        return unavailable('declared map, stratum and phase identities required')
    selected = [r for r in rows if r['round'] >= 0]
    index = {(r['scenario'], r['round'], r['variant']): r for r in selected}
    if len(index) != len(selected):
        return unavailable('duplicate paired observation')
    expected = {(s, n, v) for s in roster for n in rounds for v in variants}
    if set(index) != expected:
        return unavailable('observed scenario/round/variant roster differs from declared protocol')
    map_roster = {}
    for s in roster.values():
        if s.get('control', False):
            continue
        m = map_roster.setdefault(s['map_id'], {'stratum': s['group'], 'phases': {}})
        if m['stratum'] != s['group'] or s['phase'] in m['phases']:
            return unavailable('map appears in multiple strata or duplicate map phase')
        m['phases'][s['phase']] = s['id']
    if not map_roster or any(set(m['phases']) != set(required_phases) for m in map_roster.values()):
        return unavailable('declared primary maps require every phase exactly once')
    if any(len([m for m in map_roster.values() if m['stratum'] == g]) < 2
           for g in {m['stratum'] for m in map_roster.values()}):
        return unavailable('at least two declared independent maps per stratum required')
    output = {}
    for candidate in candidates:
        errors = []
        phase_logs = {m: {p: [] for p in required_phases} for m in map_roster}
        for sid, scenario in roster.items():
            for n in rounds:
                b, c = index[sid, n, control], index[sid, n, candidate]
                if any(any(r.get(k) != scenario[k] for k in names)
                       or bool(r.get('control', False)) != bool(scenario.get('control', False)) for r in (b, c)):
                    errors.append('paired map/stratum/phase/control identity mismatch'); continue
                if not b.get('valid', False) or not c.get('valid', False):
                    errors.append('invalid paired sample'); continue
                if b.get('resource_contaminated', False) or c.get('resource_contaminated', False):
                    errors.append('resource contamination prevents acceptance'); continue
                if any(r.get('diagnostic_stage', False) or r['result'].get('benchmark_diagnostics_enabled') for r in (b, c)):
                    errors.append('diagnostic instrumentation prevents acceptance'); continue
                signature = ('initialChecksum', 'finalChecksum', 'ticks', 'benchmark_measured_ticks')
                if any(k not in r['result'] for r in (b, c) for k in signature) or any(b['result'][k] != c['result'][k] for k in signature):
                    errors.append('paired fixed simulation window identity mismatch'); continue
                try:
                    cpu_values = [measurement(r['result'])['cpu_per_tick'] for r in (b, c)]
                    if any(not math.isfinite(v) or v <= 0 for v in cpu_values):
                        raise ValueError('invalid CPU value')
                    ratio = cpu_values[1] / cpu_values[0]
                    if not math.isfinite(ratio) or ratio <= 0:
                        raise ValueError('invalid ratio')
                except (KeyError, ValueError, ZeroDivisionError):
                    errors.append('positive finite paired CPU measurements required'); continue
                if not scenario.get('control', False):
                    phase_logs[scenario['map_id']][scenario['phase']].append(math.log(ratio))
        if errors:
            output[candidate] = {'available': False, 'errors': sorted(set(errors))}; continue
        strata = {}
        for mid, m in map_roster.items():
            # Average repetitions within phase, phases within map, maps within
            # stratum and finally strata. Resample only independent whole maps.
            value = statistics.mean(statistics.mean(phase_logs[mid][p]) for p in required_phases)
            strata.setdefault(m['stratum'], []).append(value)
        rng = random.Random(1045)
        boot = sorted(math.exp(statistics.mean(statistics.mean(rng.choices(v, k=len(v))) for v in strata.values())) for _ in range(draws))
        upper = boot[int(.95 * (draws - 1))]
        output[candidate] = {'available': True,
            'ratio': math.exp(statistics.mean(statistics.mean(v) for v in strata.values())),
            'upper_one_sided95': upper, 'cpu_target_pass': upper <= .7,
            'independent_maps': len(map_roster), 'maps_per_stratum': {g: len(v) for g, v in strata.items()},
            'rounds_per_phase': len(rounds), 'weighting': 'equal strata, maps, phases; paired rounds within phase',
            'note': 'complete declared coverage; phases and repetitions clustered by independent map; held-out confirmation and rendered guards required'}
    return output
