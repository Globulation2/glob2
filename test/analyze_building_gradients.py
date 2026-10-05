"""Analyze opt-in --telemetry building-gradients CSVs; estimates are not speedups."""
import argparse
import collections
import csv
import json
import math
from pathlib import Path


def percentile(values, p):
    values = sorted(values)
    if not values:
        return 0
    position = (len(values)-1)*p/100
    lo = math.floor(position)
    hi = math.ceil(position)
    return values[lo]+(values[hi]-values[lo])*(position-lo)


def distribution(values):
    return {f'p{p}': percentile(values, p) for p in (50, 95, 99, 100)}


def analyze(directory):
    with (directory/'building-gradients-ticks.csv').open() as stream:
        ticks = {int(r['tick']): {k: int(v) for k, v in r.items()} for r in csv.DictReader(stream)}
    cpu_fields = collections.defaultdict(lambda: collections.defaultdict(int))
    cpu_durations = collections.Counter()
    fields = collections.defaultdict(lambda: collections.defaultdict(int))
    phases = collections.Counter()
    swim_times = collections.Counter()
    stale_snapshot_ns = 0
    kinds = collections.Counter()
    reasons = collections.Counter()
    finishes = collections.Counter()
    finish_cpu = collections.Counter()
    cache = collections.Counter()
    hiring_batches = collections.Counter()
    durations = collections.Counter()
    entries = collections.Counter()
    epoch_fields = collections.defaultdict(set)
    field_resumes = collections.Counter()
    spans = collections.defaultdict(list)
    count = 0
    with (directory/'building-gradients-events.csv').open() as stream:
        for r in csv.DictReader(stream):
            count += 1
            tick = int(r['tick'])
            assert tick in ticks, 'event outside captured tick'
            kind = r['kind']
            ns = int(r['duration_ns'])
            cpu_ns = int(r.get('cpu_ns', 0))
            cpu_durations[kind] += cpu_ns
            kinds[kind] += 1
            durations[kind] += ns
            entries[kind] += int(r['popped'])
            if kind == 'hiring_batch':
                hiring_batches[r['reason']] += 1
            if kind == 'rebuild':
                reasons[r['reason']] += 1
            if kind == 'finish':
                finishes[r['reason']] += 1
                finish_cpu[r['reason']] += cpu_ns
            if kind in ('buffer', 'evict', 'invalidate'):
                cache[f"{kind}:{r['reason']}"] += 1
            if kind in ('rebuild', 'resume', 'finish'):
                key = (int(r['gid']), int(r['swim']))
                fields[tick][key] += ns
                cpu_fields[tick][key] += cpu_ns
                start = int(r["start_ns"])
                spans[tick].append((start, start+ns))
                phases[r['phase']] += ns
                swim_times[r['swim']] += ns
                if kind != 'rebuild' and r['generation'] != r['snapshot_generation']:
                    stale_snapshot_ns += ns
                epoch_fields[(tick, r['phase'], r['generation'])].add(key)
                if kind in ('resume', 'finish'):
                    field_resumes[(tick, key)] += 1
    total_tick_ns = sum(r['tick_ns'] for r in ticks.values())
    building_ns = sum(sum(j.values()) for j in fields.values())
    union_ns = 0
    overlap_ticks = 0
    for intervals in spans.values():
        end = -1
        overlap = False
        for begin, finish in sorted(intervals):
            if begin < end:
                overlap = True
            union_ns += max(0, finish-max(begin, end))
            end = max(end, finish)
        overlap_ticks += overlap
    active = list(fields)
    counts = [len(fields[t]) for t in ticks]
    building_times = [sum(fields[t].values())/1e6 for t in ticks]
    building_counts = [len({gid for gid, swim in fields[t]}) for t in ticks]
    substantial_counts = [sum(ns >= 100000 for ns in fields[t].values()) for t in ticks]
    dominant_shares = [max(j.values())/sum(j.values()) for j in fields.values() if j]
    # Optimistic lower bound: all measured work for one field forms one job,
    # fields have no dependencies, worker dispatch/publication is free. This
    # describes available work, not an implementable schedule or measured gain.
    bounds = {}
    for cores in (2, 4, 8):
        remaining = sum(max(max(j.values()), sum(j.values())/cores) for j in fields.values() if j)
        bounds[str(cores)] = {
            'building_work_lower_bound_ms': remaining/1e6,
            'whole_tick_speedup_ceiling': None if overlap_ticks else total_tick_ns/(total_tick_ns-building_ns+remaining),
        }
    total_tick_cpu = sum(r.get('tick_cpu_ns', 0) for r in ticks.values())
    building_cpu = sum(sum(j.values()) for j in cpu_fields.values())
    mode = json.loads((directory/'result.json').read_text()).get('compute_experiments') if (directory/'result.json').exists() else 'unknown'
    cpu_bounds = {}
    if total_tick_cpu and mode in ('ai', 'none'):
        for cores in (2, 4, 8):
            remaining = sum(max(max(j.values()), sum(j.values())/cores) for j in cpu_fields.values() if j)
            cpu_bounds[str(cores)] = dict(building_work_lower_bound_ms=remaining/1e6,
                simulation_thread_cpu_speedup_ceiling=total_tick_cpu/(total_tick_cpu-building_cpu+remaining))
    worst = []
    for t in sorted(ticks, key=lambda t: sum(fields[t].values()), reverse=True)[:20]:
        jobs = fields[t]
        worst.append(dict(tick=t, **{k:v for k,v in ticks[t].items() if k != 'tick'},
                          building_ms=sum(jobs.values())/1e6, building_cpu_ms=sum(cpu_fields[t].values())/1e6, distinct_fields=len(jobs),
                          largest_field_share=max(jobs.values(), default=0)/max(1,sum(jobs.values()))))
    late_cutoff = percentile(list(ticks), 75)
    late = [t for t in ticks if t >= late_cutoff]
    late_ns = sum(sum(fields[t].values()) for t in late)
    return dict(directory=str(directory.resolve()), ticks=len(ticks), first_tick=min(ticks), last_tick=max(ticks),
                events=count, dropped_events=sum(r['dropped_events'] for r in ticks.values()),
                buildings=distribution([r['buildings'] for r in ticks.values()]),
                physical_buildings=distribution([r['buildings']-r['flags'] for r in ticks.values()]),
                flags=distribution([r['flags'] for r in ticks.values()]),
                units=distribution([r['units'] for r in ticks.values()]),
                retained_fields=distribution([r['fields'] for r in ticks.values()]),
                unfinished_fields=distribution([r['unfinished'] for r in ticks.values()]),
                tick_ms=distribution([r['tick_ns']/1e6 for r in ticks.values()]),
                building_ms_per_tick=distribution(building_times),
                tick_cpu_ms=distribution([r.get('tick_cpu_ns', 0)/1e6 for r in ticks.values()]),
                building_cpu_ms_per_tick=distribution([sum(cpu_fields[t].values())/1e6 for t in ticks]),
                building_cpu_share_of_simulation_thread=building_cpu/total_tick_cpu if total_tick_cpu and mode in ('ai','none') else None,
                cpu_duration_ms={k:v/1e6 for k,v in cpu_durations.items()},
                optimistic_independent_field_cpu_bounds=cpu_bounds, distinct_fields_per_tick=distribution(counts),
                distinct_buildings_per_tick=distribution(building_counts),
                ticks_with_multiple_buildings=sum(n>=2 for n in building_counts),
                substantial_fields_per_tick_100us=distribution(substantial_counts),
                substantial_cpu_fields_per_tick_100us=distribution([sum(ns>=100000 for ns in cpu_fields[t].values()) for t in ticks]),
                ticks_with_multiple_substantial_cpu_fields_100us=sum(sum(ns>=100000 for ns in cpu_fields[t].values())>=2 for t in ticks),
                ticks_with_multiple_substantial_fields_100us=sum(n>=2 for n in substantial_counts),
                dominant_field_share_on_active_ticks=distribution(dominant_shares),
                ticks_with_work=len(active), ticks_with_multiple_fields=sum(n>=2 for n in counts),
                ticks_with_multiple_fields_pct=100*sum(n>=2 for n in counts)/len(ticks),
                building_time_share=union_ns/total_tick_ns, summed_building_work_ms=building_ns/1e6,
                ticks_with_overlapping_building_work=overlap_ticks,
                work_on_multi_field_ticks_share=sum(sum(j.values()) for j in fields.values() if len(j)>=2)/max(1,building_ns),
                same_phase_generation_groups_with_multiple_fields=sum(len(s)>=2 for s in epoch_fields.values()),
                repeated_field_extensions=sum(n-1 for n in field_resumes.values() if n>1),
                hiring_batches=dict(hiring_batches), kinds=dict(kinds), rebuild_reasons=dict(reasons), full_completion_callers=dict(finishes),
                full_completion_cpu_ms={k:v/1e6 for k,v in finish_cpu.items()},
                additional_round_trip_cpu_ms=max(0,cpu_durations['round_trip']-finish_cpu['round_trip'])/1e6,
                additional_round_trip_cpu_share_of_simulation_thread=max(0,cpu_durations['round_trip']-finish_cpu['round_trip'])/total_tick_cpu if total_tick_cpu and mode in ('ai','none') else None,
                cache=dict(cache), duration_ms={k:v/1e6 for k,v in durations.items()}, popped_entries=dict(entries),
                phase_ms={k:v/1e6 for k,v in phases.items()}, swim_class_ms={k:v/1e6 for k,v in swim_times.items()},
                stale_snapshot_propagation_ms=stale_snapshot_ns/1e6, optimistic_independent_field_bounds=bounds,
                final_quarter=dict(ticks=len(late), building_time_share=late_ns/sum(ticks[t]['tick_ns'] for t in late),
                                   buildings=distribution([ticks[t]['buildings'] for t in late]),
                                   distinct_fields_per_tick=distribution([len(fields[t]) for t in late])),
                worst_ticks=worst)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', type=Path, nargs='+')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = {'bound_note': 'Optimistic cost bounds, ignoring dependencies, speculative work, dispatch and memory bandwidth; not measured speedups.',
              'runs': [analyze(p) for p in args.directories]}
    text = json.dumps(result, indent=2)+'\n'
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
