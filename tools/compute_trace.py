#!/usr/bin/env python3
"""Analyze a drained compute_telemetry trace from a headless result or session export."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def analyze(document):
    telemetry = document.get('compute_telemetry', document)
    if telemetry.get('schema_version') != 1:
        raise ValueError('unsupported compute telemetry schema')
    if telemetry.get('mode') != 'trace' or not telemetry.get('trace_included'):
        raise ValueError('a drained trace export is required')
    streams = telemetry['trace_streams']
    lost = sum(stream['lost_events'] for stream in streams)
    # Timestamps order different streams; stream position breaks timestamp ties.
    events = sorted((event['timestamp_ns'], stream['stream'], position, event)
                    for stream in streams for position, event in enumerate(stream['events']))
    batches, jobs, lane_completed = {}, {}, {}
    waits = []
    for _, _, _, event in events:
        kind = event.get('execution_kind', 0)
        identity = kind, event['serial']
        batch = batches.setdefault(identity, {'submitted': None, 'completed': None, 'inspection': None})
        name, timestamp = event['event'], event['timestamp_ns']
        if name == 'submit':
            batch['submitted'] = timestamp
        elif name == 'join_inspection':
            batch['inspection'] = timestamp
        elif name == 'join_end':
            waits.append({'execution_kind': kind, 'serial': identity[1], 'producer': event['producer'],
                          'join_elapsed_ns': timestamp-batch['inspection'] if batch['inspection'] is not None else None,
                          'completion_to_join_end_ns': timestamp-batch['completed'] if batch['completed'] is not None else None})
        elif name in ('claim', 'callback_start', 'callback_end', 'suspend', 'external_resolution', 'runnable', 'complete'):
            key = identity + (event['job'],)
            job = jobs.setdefault(key, {'execution_kind': kind, 'serial': identity[1], 'job': event['job'],
                                       'producer': event['producer'], 'lane': event['lane'],
                                       'lane_sequence': event['lane_sequence'], 'claimed': None,
                                       'starts': [], 'ends': [], 'suspended_callbacks': [], 'resolutions': [],
                                       'complete': None, 'predecessor_complete': None})
            if name == 'claim' and job['claimed'] is None:
                job['claimed'] = timestamp
                if kind == 0 and event['lane'] != 4294967295 and event['lane_sequence']:
                    job['predecessor_complete'] = lane_completed.get((event['lane'], event['lane_sequence']-1))
            elif name == 'callback_start':
                job['starts'].append(timestamp)
            elif name == 'callback_end':
                job['ends'].append(timestamp)
            elif name == 'suspend':
                job['suspended_callbacks'].append(len(job['starts'])-1)
            elif name == 'external_resolution':
                job['resolutions'].append(timestamp)
            elif name == 'complete':
                job['complete'] = timestamp
                batch['completed'] = timestamp
                if kind == 0 and event['lane'] != 4294967295:
                    lane_completed[event['lane'], event['lane_sequence']] = timestamp
    rows = []
    for key, job in sorted(jobs.items()):
        submitted = batches[key[:2]]['submitted']
        claimed = job['claimed']
        predecessor = job['predecessor_complete']
        needs_predecessor = key[0] == 0 and job['lane'] != 4294967295 and job['lane_sequence'] > 0
        ready = max(submitted, predecessor) if submitted is not None and predecessor is not None else submitted
        if needs_predecessor and predecessor is None:
            ready = None  # Trace may start after the predecessor or lose its event.
        paired = len(job['starts']) == len(job['ends']) and all(b >= a for a, b in zip(job['starts'], job['ends']))
        external = None
        if job['suspended_callbacks'] and len(job['suspended_callbacks']) == len(job['resolutions']) and paired:
            external = sum(max(0, resolution-job['ends'][index])
                           for index, resolution in zip(job['suspended_callbacks'], job['resolutions'])
                           if 0 <= index < len(job['ends']))
            if any(index < 0 or index >= len(job['ends']) for index in job['suspended_callbacks']):
                external = None
        rows.append({k: job[k] for k in ('execution_kind', 'serial', 'job', 'producer', 'lane', 'lane_sequence')} | {
            'callback_count': len(job['starts']),
            'callback_elapsed_ns': sum(b-a for a, b in zip(job['starts'], job['ends'])) if paired else None,
            'lane_dependency_ns': max(0, predecessor-submitted) if predecessor is not None and submitted is not None else (None if needs_predecessor else 0),
            'eligible_queue_ns': claimed-ready if claimed is not None and ready is not None else None,
            'external_completion_ns': external,
            'completed': job['complete'] is not None,
        })
    return {'schema_version': 1, 'incomplete': bool(lost) or telemetry.get('trace_incomplete', False), 'lost_events': lost, 'jobs': rows, 'joins': waits}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    text = args.input.read_text()
    if text.startswith('GLOB2_COMPUTE_SUMMARY '):
        text = text.split(' ', 1)[1]
    try:
        result = analyze(json.loads(text))
    except ValueError as error:
        parser.error(str(error))
    output = json.dumps(result, indent=2)+'\n'
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end='')


if __name__ == '__main__':
    main()
