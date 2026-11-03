#!/usr/bin/env python3
"""Frozen, independent-seed movement-ratio experiments through tools.tournaments."""
import argparse
from collections import Counter, defaultdict
import csv
import copy
import gzip
import shutil
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys
import time

from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json, digest, read_json
from tools.tournaments.jobs import routing_ratio_outputs
from tools.tournaments.model import job, validate_experiment
from tools.tournaments.results import Results
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.storage import directory_bytes
from tools.tournaments.probability import group_score

ARMS = ('baseline', 'mode', 'speed-modes', 'median', 'equal')
NAMED = {'bastion-keys': 'water', 'plantations': 'water', 'lava-shield': 'land',
         'city-states': 'land', 'canals': 'mixed', 'braided-river': 'mixed'}
WATER = {'isles', 'islands', 'rugged-archipelago', 'coral', 'shattered-coast', 'tidal-flats', 'concrete-islands', 'honeycomb-isle'}
MIXED = {'river', 'braided-delta', 'hilbert-river', 'watershed', 'swamp', 'everglades', 'fjord-continent', 'crater-lakes', 'portage-lakes', 'drowned-forest'}
GIB = 1024**3


def category(g):
    return NAMED.get(g['id'], 'water' if g['id'] in WATER else 'mixed' if g['id'] in MIXED else 'land')


def plan(bundle, *, phase, setups=12, replicates=4, seed=20261102, arms=ARMS,
         ticks=90000, diagnostics=.2, deadline=None, quality_fraction=1, process_attempts=2):
    if setups < 2 or setups % 2 or replicates < 4 or not 0 <= diagnostics <= 1:
        raise ValueError('use an even setup count, at least four independent replicates, and diagnostic fraction 0..1')
    if not 0<=quality_fraction<=1 or process_attempts<1:raise ValueError('invalid diagnostic quality fraction or attempt count')
    if not arms or arms[0] != 'baseline' or len(set(arms)) != len(arms) or set(arms)-set(ARMS):
        raise ValueError('arms must start with baseline and contain distinct known policies')
    catalog = bundle['capabilities']
    generators = [g for g in catalog['generators'] if not g.get('editorOnly')]
    missing = set(NAMED)-{g['id'] for g in generators}
    if missing:
        raise ValueError('pinned catalog is missing requested generators: '+', '.join(sorted(missing)))
    ais = [a['name'] for a in catalog['ais']]
    rng = random.Random(digest([seed, phase]))
    named = [g for g in generators if g['id'] in NAMED]
    by_category = {c: [g for g in generators if g['id'] not in NAMED and category(g)==c] for c in ('land','water','mixed')}
    for values in [named, *by_category.values()]:
        rng.shuffle(values)
    selected = []
    for i in range(setups//2):
        selected.append(named[i % len(named)])
        c = ('land','water','mixed')[i % 3]
        selected.append(by_category[c][(i//3) % len(by_category[c])])
    setup_rows = []
    for i,g in enumerate(selected):
        fmt = ('1v1','2v2','ffa')[i % 3]
        teams = 2 if fmt=='1v1' else 4 if fmt=='2v2' else rng.choice((3,4,6))
        controls = {c['id']: c for c in g['controls']}
        sizes = [v for v in controls['width']['values'] if 7 <= v <= 9 and v in controls['height']['values']]
        if not sizes: raise ValueError('generator lacks a supported campaign size')
        size = sizes[(i//3) % len(sizes)]
        # Scalar catalog ranges do not describe joint density restrictions.
        # Use normal generator layouts; vary seeds, sizes, formats and AIs.
        # Reserve 128-square setups for two teams, and respect estate/court space.
        size=next(v for v in sizes if v>=size and (teams<=2 or v>=8)
            and (g['id']!='bastion-keys' or teams<=(2**v//128)**2)
            and (g['id']!='hilbert-river' or v>=8))
        params={c['id']:c.get('default',c['values'][0]) for c in g['controls']
                if c['id'] not in ('width','height','teams','workers')}
        params.update(width=size,height=size,teams=teams)
        # Alternate homogeneous and mixed AI lineups; rotate seats by replicate.
        players = [rng.choice(ais)]*teams if i % 3==0 else [rng.choice(ais) for _ in range(teams)]
        setup_rows.append(dict(id=digest([phase,i,g['method'],params,players])[:16],generator=g['method'],
                               generator_id=g['id'],category=category(g),format=fmt,params=params,players=players))
    jobs = []
    # Complete minimum replication within each randomized setup before moving
    # on. A budget stop sacrifices setup breadth, not every setup's precision.
    setup_order=list(setup_rows);rng.shuffle(setup_order)
    extra=round(setups*replicates*diagnostics/(1-diagnostics)) if 0<diagnostics<1 else 0
    blocks=[]
    for index,setup in enumerate(setup_order):
        local=[(setup,r,diagnostics==1) for r in range(replicates)]
        local += [(setup,replicates+r,True) for r in range(extra//setups+(index<extra%setups))]
        rng.shuffle(local);blocks.extend(local)
    diagnostic_blocks=[i for i,b in enumerate(blocks) if b[2]]
    # Select whole randomized blocks so every arm gets the same observer tier.
    quality_order=sorted(diagnostic_blocks,key=lambda i:digest([seed,phase,i,'quality-tier']))
    quality_blocks=set(quality_order[:round(len(quality_order)*quality_fraction)])
    for block_index,(setup,replicate,diagnostic) in enumerate(blocks):
        order = list(arms);rng.shuffle(order)
        for arm in order:
            ratio = dict(policy=arm,study='routes' if diagnostic else 'counts',work=True,quality=diagnostic and block_index in quality_blocks,kernels=diagnostic)
            players=setup['players'][replicate % len(setup['players']):]+setup['players'][:replicate % len(setup['players'])]
            config=dict(generator=setup['generator'],params=setup['params'],candidates=5,players=players,ticks=ticks,
                        win_probability_permille=950,routing_ratio=ratio)
            if setup['format']=='2v2':config['alliances']=[1,1,2,2]
            limits=dict(timeout_seconds=1800,artifact_bytes=512*1024**2,log_bytes=128*1024**2)
            if deadline is not None:limits['deadline_unix']=deadline
            jobs.append(job('game',bundle['id'],seeds={'map':rng.randrange(2**32),'game':rng.randrange(2**32)},
                config=config,outputs=dict(telemetry=['team-timeline'],log_mode='research',result='full',
                    required=routing_ratio_outputs(ratio),saves=['initial','final'] if diagnostic and replicate==(0 if diagnostics==1 else replicates) and setup['generator_id'] in NAMED else [],
                    replay=diagnostic and replicate==(0 if diagnostics==1 else replicates) and setup['generator_id'] in NAMED),
                limits=limits,labels=dict(phase=phase,setup=setup['id'],replicate=replicate,block=block_index,
                    variant=arm,format=setup['format'],generator=setup['generator'],generator_id=setup['generator_id'],
                    terrain_category=setup['category'],diagnostic=diagnostic,quality_probe=ratio['quality'],held_out=phase=='confirmation')))
    manifest=dict(schema_version=1,id='ratio-'+phase+'-'+digest([bundle['id'],seed,setups,replicates,list(arms),ticks,diagnostics,quality_fraction,process_attempts])[:16],
        settings=dict(artifact_budget_bytes=30*GIB,disk_reserve_bytes=10*GIB,prefetch=0,process_attempts=process_attempts),jobs=jobs,
        metadata=dict(contract='routing-ratio-study-v1',phase=phase,setups=setup_rows,seed=seed,
            independent_seeds=True,minimum_replicates=replicates,stopping_permille=950,quality_probe_fraction=quality_fraction,
            routing_observer=dict(query_hash_denominator=1024,window_ticks=512,routes_per_window=8,routes_per_game=2048),
            adaptive_model_sha256=bundle.get('build_options',{}).get('adaptive_model_sha256'),
            layout_controls='pinned catalog defaults with legal size/density constraints',
            category_source='predeclared generator strata; measure realized terrain separately'))
    validate_experiment(manifest)
    return manifest


def add_observer_calibration(manifest):
    """Same simulation seeds isolate observer cost; excluded from policy inference."""
    originals=[j for j in manifest['jobs'] if j['labels']['variant']=='baseline' and not j['labels']['diagnostic']]
    selected_setups=list(dict.fromkeys(j['labels']['setup'] for j in originals))[:2]
    rng=random.Random(4517);calibration=[]
    for original in originals:
        if original['labels']['setup'] not in selected_setups:continue
        modes=['off','counts','routes'];rng.shuffle(modes)
        for mode in modes:
            config=copy.deepcopy(original['config']);config['ticks']=min(8192,config['ticks'])
            config['routing_ratio']=dict(policy='baseline',study=mode,work=mode!='off',quality=mode=='routes',kernels=mode=='routes')
            labels=dict(original['labels'],diagnostic=True,observer_mode=mode)
            calibration.append(job('game',original['build'],seeds=original['seeds'],config=config,
                outputs=dict(telemetry=['team-timeline'],log_mode='research',result='full',required=routing_ratio_outputs(config['routing_ratio'])),
                limits=original['limits'],labels=labels))
    manifest['jobs']=calibration+manifest['jobs']
    return manifest


def observer_overhead(source):
    groups=defaultdict(dict);checksums=defaultdict(dict)
    for r in source:
        label=r['job']['labels']
        if r['category']!='success' or 'observer_mode' not in label:continue
        result=r['result'];ticks=result.get('benchmark_measured_ticks',0)
        if ticks:groups[label['setup'],label['replicate']][label['observer_mode']]=result['benchmark_run_cpu_ns']/1e9/ticks
        checksums[label['setup'],label['replicate']][label['observer_mode']]=result.get('finalChecksum')
    mismatches=[str(k) for k,v in checksums.items() if len({str(c) for c in v.values()})>1]
    if mismatches:raise RuntimeError('observer calibration checksum mismatch: '+', '.join(mismatches))
    output={}
    for mode in ('counts','routes'):
        penalties=[v[mode]/v['off']-1 for v in groups.values() if mode in v and v.get('off',0)>0]
        output[mode]=describe(penalties)
    return dict(relative_cpu_overhead=output,checksum_groups=len(checksums),checksum_mismatches=mismatches,scope='8192-tick calibration on two pilot setups; same seeds isolate observers, separate from independent policy comparisons')


def rows(source, record, suffix):
    name='routing-ratio.'+suffix+'.csv'
    if name not in {a['path'] for a in record['artifacts']}:return []
    with source.open_artifact(record,name) as stream:
        return list(csv.DictReader(stream))


def quantile(values, q):
    values=sorted(values)
    if not values:return None
    position=(len(values)-1)*q;lo=int(position);fraction=position-lo
    return values[lo]*(1-fraction)+values[min(lo+1,len(values)-1)]*fraction


def describe(values):
    values=[float(v) for v in values if v is not None and math.isfinite(float(v))]
    return dict(n=len(values),mean=statistics.mean(values) if values else None,
                median=quantile(values,.5),p95=quantile(values,.95),p99=quantile(values,.99))


def freeze_stages(source):
    """Freeze tick boundaries using only baseline-pilot observed workload growth."""
    boundaries=defaultdict(list)
    for record in source:
        labels=record['job']['labels']
        if record['category']!='success' or labels['variant']!='baseline' or labels['phase']!='pilot' or 'observer_mode' in labels:continue
        timeline=defaultdict(float)
        for row in rows(source,record,'state'):
            timeline[int(row['tick'])]+=float(row['population'])+4*float(row['buildings'])
        if not timeline:continue
        low=timeline[min(timeline)];high=max(timeline.values())
        if high<=low:continue
        cuts=[next(t for t in sorted(timeline) if timeline[t]>=low+fraction*(high-low)) for fraction in (.4,.8)]
        key=(labels['generator_id'],labels['format'],record['job']['config']['params']['width'])
        boundaries[key].append(cuts)
    fallback=defaultdict(list)
    for (_,fmt,width),values in boundaries.items():fallback[fmt,width].extend(values)
    return dict(fallback=[dict(format=k[0],width=k[1],mid_tick=statistics.median(v[0] for v in values),
        late_tick=statistics.median(v[1] for v in values),games=len(values)) for k,values in sorted(fallback.items())],
        contract='baseline-pilot-workload-stage-v1',source=digest(source.manifest),boundaries=[dict(
        generator=k[0],format=k[1],width=k[2],mid_tick=statistics.median(v[0] for v in values),
        late_tick=statistics.median(v[1] for v in values),games=len(values)) for k,values in sorted(boundaries.items())])


def stage_for(job,tick,stages):
    label=job['labels'];width=job['config']['params']['width']
    found=next((r for r in stages.get('boundaries',[]) if (r['generator'],r['format'],r['width'])==(label['generator_id'],label['format'],width)),None)
    if not found:found=next((r for r in stages.get('fallback',[]) if (r['format'],r['width'])==(label['format'],width)),None)
    if not found:return 'unclassified'
    return 'early' if tick<found['mid_tick'] else 'mid' if tick<found['late_tick'] else 'late'


def latest_gameplay(source,record):
    """Decode only final counters; retain the full interval log as evidence."""
    if 'stdout.log' not in {a['path'] for a in record['artifacts']}:return {}
    from tools.tournaments.game_telemetry import parse
    header=re.compile(r'^GLOB2_MEASURE team=(\d+) tick=(\d+)(?:\s|$)')
    latest={}
    with source.open_artifact(record,'stdout.log') as stream:
        for line in stream:
            if not line.startswith('GLOB2_MEASURE '):continue
            match=header.match(line)
            if match:team,tick=map(int,match.groups())
            else:
                values=parse(line)[1];team=values.get('team');tick=values.get('tick',-1)
                if team is None:continue
            if tick>=latest.get(team,(-1,None))[0]:latest[team]=(tick,line)
    return {team:parse(value[1])[1] for team,value in latest.items()}


def export_games(source, stages):
    output=[]
    for record in source:
        if record['category']!='success':continue
        job=record['job'];labels=job['labels'];result=record['result']
        if 'observer_mode' in labels:continue
        kernel_collected=job['config']['routing_ratio'].get('kernels',False)
        work_collected=job['config']['routing_ratio'].get('work',True)
        states=rows(source,record,'state');queries=rows(source,record,'queries');work=rows(source,record,'work');kernel=rows(source,record,'kernel')
        query_windows=defaultdict(Counter)
        for row in queries:
            if row['planning']=='0' and int(row['family'])<7 and 1<=int(row['physical'])<=6:
                query_windows[int(row['window']),int(row['team'])][int(row['physical'])]+=int(row['queries'])
        diverse=set()
        for row in states:
            tick=int(row['tick']);key=(tick//512,int(row['team']))
            population=[int(row['class'+str(c)]) for c in range(1,7)]
            demand=query_windows[key];total=sum(demand.values())
            mode=1+population.index(max(population))
            if sum(n>0 for n in population)>=3 and len(demand)>=3 and total and sum(n/total>=.05 for c,n in demand.items() if c!=mode)>=2:
                diverse.add(key)
        route_rows=rows(source,record,'routes');valid=[r for r in route_rows if r['baseline_status']==r['shared_status']=='goal']
        penalties=[float(r['shared_ticks'])/float(r['baseline_ticks'])-1 for r in valid if float(r['baseline_ticks'])>0]
        process=rows(source,record,'process')
        sharing_masks=defaultdict(int)
        sharing_name='gradient-work.csv.sharing.csv'
        if sharing_name in {a['path'] for a in record['artifacts']}:
            with source.open_artifact(record,sharing_name) as stream:
                for r in csv.DictReader(stream):
                    sharing_masks[(int(r['window']),int(r['family']),int(r['team']),r['material'],r['destination'])]|=int(r['movement_mask'])&126
        overlap=sum(mask.bit_count()-1 for mask in sharing_masks.values() if mask.bit_count()>1)
        computed_windows={(key[0],key[2]) for key,mask in sharing_masks.items() if mask.bit_count()>1}
        elapsed=max((float(r['elapsed_seconds']) for r in process),default=None)
        cpu=max((float(r['process_cpu_seconds']) for r in process if r['process_cpu_seconds']),default=None)
        if result.get('benchmark_measured_ticks',0)>0:
            cpu=result['benchmark_run_cpu_ns']/1e9
        rss=max((int(r['rss_bytes']) for r in process if r.get('rss_bytes')),default=None)
        usage=record.get('resource_usage',{})
        entry=dict(job_id=job['id'],phase=labels['phase'],setup=labels['setup'],arm=labels['variant'],slot=(record.get('execution_allocation') or {}).get('slot'),
            block=labels['block'],generator=labels['generator_id'],format=labels['format'],diagnostic=labels['diagnostic'],
            termination=result.get('termination'),outcomes=dict(Counter(t['outcome'] for t in result.get('teams',[]))),
            width=job['config']['params']['width'],teams=len(job['config']['players']),ticks=result['ticks'],
            involuntary_context_switches=usage.get('involuntary_context_switches'),voluntary_context_switches=usage.get('voluntary_context_switches'),
            process_cpu_seconds=usage.get('user_seconds',0)+usage.get('system_seconds',0),wall_seconds=record['finished']-record['started'],
            simulation_cpu_seconds=cpu,simulation_wall_seconds=elapsed,simulation_sampled_rss_peak=rss,tick_p50_ns=result.get('tick_p50_ns'),tick_p95_ns=result.get('tick_p95_ns'),tick_p99_ns=result.get('tick_p99_ns'),route_sampling_capped=max((int(r.get('route_capped',0)) for r in process),default=0),peak_rss_bytes=usage.get('peak_rss_bytes'),
            high_diversity_team_windows=len(diverse),confirmed_extra_ratio_slots=overlap,high_diversity_with_extra_builds=len(diverse&computed_windows),observed_team_windows=len({(int(r['tick'])//512,int(r['team'])) for r in states}),route_time_penalty=describe(penalties),
            correctness_failures=sum(r['baseline_status']=='goal' and r['shared_status']=='unreachable' for r in route_rows),
            route_failures=dict(Counter(r['shared_status'] for r in route_rows if r['shared_status']!='goal')),
            direction_changed=describe([int(r['first_direction_changed']) for r in route_rows]),
            live_direction_changed=describe([int(r['live_first_direction_changed']) for r in route_rows if r['live_first_direction_changed']]),
            kernel_collected=kernel_collected,expanded_cells=sum(int(r['expanded_cells']) for r in kernel) if kernel_collected else None,admissions=sum(int(r['admissions']) for r in work) if work_collected else None,
            publications=sum(int(r['publications']) for r in work) if work_collected else None,families={},scores=[])
        for family in range(7):
            selected=[r for r in work if int(r['family'])==family]
            entry['families'][str(family)]={key:sum(int(r[key]) for r in selected) for key in ('admissions','publications')}
            field_kernel=[r for r in kernel if int(r['family'])==family]
            entry['families'][str(family)].update({key:sum(int(r[key]) for r in field_kernel) if kernel_collected else None for key in ('expanded_cells','queue_entries','builds','initialized_cells')})
        memory_keys=('field_arrays_bytes','search_queue_bytes','idle_buffer_bytes','pending_output_reservation_bytes','shared_terrain_bytes')
        entry['sampled_memory_peaks']={key:max((int(r[key]) for r in states if r.get(key)),default=None) for key in memory_keys}
        histogram=Counter()
        for row in rows(source,record,'ticks'):histogram[int(row['bin_lower_ns'])]+=int(row['count'])
        total=sum(histogram.values());entry['tick_latency_bounds_ns']={}
        for q in (.5,.95,.99):
            cumulative=0
            for lower,n in sorted(histogram.items()):
                cumulative+=n
                if cumulative>=total*q:
                    entry['tick_latency_bounds_ns'][str(q)]=[lower,max(1,lower*2)];break
        previous_selection={};switch_ticks=[]
        for row in sorted(states,key=lambda r:(int(r['tick']),int(r['team']))):
            team=int(row['team']);selected=int(row['selected'])
            if row['known']=='1' and team in previous_selection and previous_selection[team]!=selected:switch_ticks.append((team,int(row['tick'])))
            previous_selection[team]=selected
        identity_name='gradient-work.csv.identity.csv'
        identities=[]
        if identity_name in {a['path'] for a in record['artifacts']}:
            with source.open_artifact(record,identity_name) as stream:identities=list(csv.DictReader(stream))
        entry['field_event_totals']={}
        for row in identities:
            key=':'.join(row[k] for k in ('family','team','selected','event','reason'))
            entry['field_event_totals'][key]=entry['field_event_totals'].get(key,0)+int(row['count'])
        interval_name='gradient-work.csv.intervals.csv'
        intervals=[]
        if interval_name in {a['path'] for a in record['artifacts']}:
            with source.open_artifact(record,interval_name) as stream:intervals=list(csv.DictReader(stream))
        n=sum(int(r['count']) for r in intervals)
        entry['refresh_interval_mean_ticks']=sum(int(r['interval'])*int(r['count']) for r in intervals)/n if n else None
        entry['policy_switches']=len(switch_ticks)
        entry['cold_admissions_in_switch_windows']=sum(int(r['count']) for r in identities if r['event']=='admission' and r['reason']=='1'
            and any(int(r['team'])==team and int(r['window'])==tick//512 for team,tick in switch_ticks))
        entry['expanded_cells_in_switch_windows']=sum(int(r['expanded_cells']) for r in kernel
            if any(int(r['team'])==team and 0<=int(r['tick'])-tick<512 for team,tick in switch_ticks)) if kernel_collected else None
        model_counts=defaultdict(Counter)
        for row in rows(source,record,'model'):
            counter=model_counts[row['family']]
            for key,value in row.items():
                if key not in ('window','family','team','selected'):counter[key]+=int(value)
        entry['adaptive_model']={k:dict(v) for k,v in model_counts.items()}
        point_rows=rows(source,record,'points')
        entry['point_queries']=sum(int(r['queries']) for r in point_rows)
        entry['point_reachable']=sum(int(r['reachable']) for r in point_rows)
        entry['route_distance_penalty']=describe([float(r['shared_distance'])/float(r['baseline_distance'])-1 for r in valid if float(r['baseline_distance'])>0])
        entry['route_extra_distance']=describe([float(r['shared_distance'])-float(r['baseline_distance']) for r in valid])
        entry['route_extra_ticks']=describe([float(r['shared_ticks'])-float(r['baseline_ticks']) for r in valid])
        entry['route_affected_fraction']=describe([float(r['shared_ticks'])>float(r['baseline_ticks'])+1e-9 for r in valid])
        entry['equal_cost_direction_changes']=sum(r['first_direction_changed']=='1' and r['equal_first_direction_cost']=='1' for r in route_rows)
        entry['non_tied_direction_changes']=sum(r['first_direction_changed']=='1' and r['equal_first_direction_cost']=='0' for r in route_rows)
        entry['target_changed']=describe([int(r['target_changed']) for r in valid])
        entry['oracle_time_penalty']=describe([float(r['shared_ticks'])/float(r['actual_shortest_ticks'])-1 for r in valid if r['actual_shortest_ticks'] and 0<float(r['actual_shortest_ticks'])<math.inf])
        entry['uniform_direction_disagreement']=sum(int(r['uniform_different']) for r in route_rows)/max(1,sum(int(r['uniform_comparable']) for r in route_rows))
        entry['adverse_routes']=[dict(r,job_id=job['id']) for r in sorted(valid,key=lambda r:float(r['shared_ticks'])-float(r['baseline_ticks']),reverse=True)[:3]]
        entry['adaptive_quality']={}
        quality_name='gradient-quality.csv'
        if quality_name in {a['path'] for a in record['artifacts']}:
            quality=defaultdict(Counter)
            with source.open_artifact(record,quality_name) as stream:
                for r in csv.DictReader(stream):
                    q=quality[r['family']];q['queries']+=1
                    for key in ('extra_cost','false_unreachable','false_reachable','invalid_goal','missed_move','escape_failure','stall'):q[key]+=int(r[key])
            entry['adaptive_quality']={k:dict(v) for k,v in quality.items()}
        latest=latest_gameplay(source,record)
        entry['gameplay']={str(team):{k:v for k,v in values.items() if k.startswith(('deaths_','completed_','harvested_','delivered_','harvestDistance_','harvestSamples_','eatWalkDistance_','eatWalkSamples_','labour_','filling_')) or k in ('tick','hungry','critical','trappedUnits','trappedBuildings','meals','trainingVisits')} for team,values in latest.items()}
        entry['starvation_deaths']=sum(v for values in entry['gameplay'].values() for k,v in values.items() if k.startswith('deaths_') and k.endswith('_1') and isinstance(v,(int,float))) if latest else None
        amphibious_queries=[r for r in queries if r['planning']=='0' and int(r['family'])<7 and 1<=int(r['physical'])<=6]
        amphibious_count=sum(int(r['queries']) for r in amphibious_queries)
        entry['selected_class_query_coverage']=sum(int(r['queries']) for r in amphibious_queries if r['physical']==r['selected'])/amphibious_count if amphibious_count else None
        entry['route_mean_time_penalty']=entry['route_time_penalty']['mean']
        entry['route_p99_time_penalty']=entry['route_time_penalty']['p99']
        entry['route_mean_extra_ticks']=entry['route_extra_ticks']['mean']
        for key,value in entry['sampled_memory_peaks'].items():entry['memory_'+key]=value
        for key in ('admissions','publications','expanded_cells'):
            entry[key+'_per_tick']=entry[key]/result['ticks'] if entry[key] is not None and result['ticks'] else None
        entry['duration_ticks']=result['ticks']
        entry['cpu_per_tick']=cpu/result['ticks'] if cpu is not None and result['ticks'] else None
        # Population/building integrals use elapsed simulation ticks, never number of rows.
        population=buildings=0;last=defaultdict(int)
        for row in sorted(states,key=lambda r:(int(r['tick']),int(r['team']))):
            team=int(row['team']);tick=int(row['tick']);dt=max(0,tick-last[team]);last[team]=tick
            population+=dt*int(row['population']);buildings+=dt*int(row['buildings'])
        entry.update(unit_ticks=population,building_ticks=buildings,
            routing_queries=sum(int(r['queries']) for r in queries if r['planning']=='0'))
        for key,denominator in [('unit_tick',population),('building_tick',buildings),('routing_query',entry['routing_queries'])]:
            entry['cpu_per_'+key]=cpu/denominator if cpu is not None and denominator else None
        for team in result.get('teams',[]):
            score=group_score([team],result,job['config'])
            entry['scores'].append(dict(team=team['team'],ai=next((p['ai'] for p in result['players'] if p['team']==team['team']),None),
                alliance=team.get('alliance'),outcome=team['outcome'],trajectory=score))
        # Preserve interval data for late-game and workload adjustment without inflating the summary.
        entry['intervals']=[];previous_cpu=previous_tick=0
        work_by_tick=defaultdict(lambda:Counter())
        for row in work:
            for metric in ('admissions','publications'):
                work_by_tick[int(row['tick'])][metric]+=int(row[metric])
        for row in kernel:
            for metric in ('expanded_cells','queue_entries','builds','initialized_cells'):
                work_by_tick[int(row['tick'])][metric]+=int(row[metric])
        for sample in process:
            if not sample['process_cpu_seconds']:continue
            tick=int(sample['tick']);completed=int(sample.get('completed_ticks',tick));current_cpu=float(sample['process_cpu_seconds']);dt=completed-previous_tick
            if dt<=0:
                if entry['intervals'] and current_cpu>previous_cpu:entry['intervals'][-1]['cpu_seconds']+=current_cpu-previous_cpu
                previous_cpu=current_cpu
                continue
            state=[r for r in states if int(r['tick'])==tick]
            entry['intervals'].append(dict(active_teams=sum(int(r['population'])+int(r['buildings'])>0 for r in state),tick=tick,stage=stage_for(job,tick,stages),ticks=dt,
                cpu_seconds=current_cpu-previous_cpu,population=sum(int(r['population']) for r in state),
                buildings=sum(int(r['buildings']) for r in state),high_diversity=any((tick//512,int(r['team'])) in diverse for r in state),
                **work_by_tick[tick]))
            previous_cpu=current_cpu;previous_tick=completed
        diverse_intervals=[r for r in entry['intervals'] if r['high_diversity']]
        entry['diverse_ticks']=sum(r['ticks'] for r in diverse_intervals)
        entry['cpu_per_diverse_tick']=sum(r['cpu_seconds'] for r in diverse_intervals)/entry['diverse_ticks'] if entry['diverse_ticks'] else None
        entry['active_team_ticks']=sum(r['ticks']*r['active_teams'] for r in entry['intervals'])
        for stage in ('early','mid','late','unclassified'):
            selected=[r for r in entry['intervals'] if r['stage']==stage]
            entry['cpu_per_tick_'+stage]=sum(r['cpu_seconds'] for r in selected)/sum(r['ticks'] for r in selected) if selected else None
        output.append(entry)
    return output


def _block_effect(treated, baseline):
    """Within-block regression for independent seeds; blocks absorb setup effects."""
    blocks=defaultdict(lambda:[[],[]])
    for row in baseline:blocks[row[1]][0].append(row[0])
    for row in treated:blocks[row[1]][1].append(row[0])
    numerator=denominator=0
    for b,a in blocks.values():
        if not a or not b:continue
        weight=len(a)*len(b)/(len(a)+len(b))
        numerator+=weight*(statistics.mean(a)-statistics.mean(b));denominator+=weight
    return numerator/denominator if denominator else None


def compare(games, metric='cpu_per_tick', draws=2000, seed=11, diagnostic=False):
    """Block-adjusted independent games; equal-setup hierarchical bootstrap."""
    rng=random.Random(seed);grouped=defaultdict(lambda:defaultdict(list))
    for g in games:
        v=g.get(metric)
        if v is not None and g['diagnostic']==diagnostic:grouped[g['setup']][g['arm']].append((v,g.get('block',0)))
    result={}
    for arm in sorted({g['arm'] for g in games}-{'baseline'}):
        eligible=[s for s,arms in grouped.items() if len(arms['baseline'])>=4 and len(arms[arm])>=4
                  and _block_effect(arms[arm],arms['baseline']) is not None]
        if not eligible:
            result[arm]=dict(setups=0,effect=None,ci95=None);continue
        effect=statistics.mean(_block_effect(grouped[s][arm],grouped[s]['baseline']) for s in eligible)
        samples=[]
        for _ in range(draws):
            effects=[]
            for s in rng.choices(eligible,k=len(eligible)):
                a,b=grouped[s][arm],grouped[s]['baseline']
                value=_block_effect(rng.choices(a,k=len(a)),rng.choices(b,k=len(b)))
                if value is not None:effects.append(value)
            if effects:samples.append(statistics.mean(effects))
        result[arm]=dict(setups=len(eligible),effect=effect,ci95=[quantile(samples,.025),quantile(samples,.975)],
            bootstrap_draws=len(samples),games={a:sum(len(grouped[s][a]) for s in eligible) for a in ('baseline',arm)},
            resampling='setup_then_independent_games_within_arm',adjustment='setup and randomized interleaving block')
    return result


def regression(games):
    """Secondary associations; whole games are the resampling unit elsewhere."""
    import numpy as np
    games=[g for g in games if not g['diagnostic'] and g['cpu_per_tick'] is not None]
    if not games:return None
    arms=sorted({g['arm'] for g in games}-{'baseline'})
    names=['arm:'+a for a in arms]+['units','buildings','active_teams','diversity_fraction']
    blocks=defaultdict(list)
    for g in games:
        values=[int(g['arm']==a) for a in arms]+[g['unit_ticks']/max(1,g['ticks']),g['building_ticks']/max(1,g['ticks']),
            g['active_team_ticks']/max(1,g['ticks']),g['diverse_ticks']/max(1,g['ticks'])]
        blocks[g['setup'],g['block']].append((values,g['cpu_per_tick']))
    x=[];y=[]
    for values in blocks.values():
        bx=np.asarray([v[0] for v in values],dtype=float);by=np.asarray([v[1] for v in values],dtype=float)
        x.extend(bx-bx.mean(axis=0));y.extend(by-by.mean())
    coefficients,_,rank,_=np.linalg.lstsq(x,y,rcond=None)
    return dict(n=len(games),rank=int(rank),columns=len(names),identifiable=int(rank)==len(names),coefficients=dict(zip(names,map(float,coefficients))),
        absorbed_covariates=['setup','randomized time block','map area','initial team count','AI composition'],
        interpretation='secondary within-block workload-adjusted association; population and duration may be treatment mediators; rank deficiency is reported')


def summarize(directory, output, stages=None):
    source=Results(directory);records=list(source)
    stages=stages or freeze_stages(source)
    games=export_games(source,stages)
    metrics=('simulation_cpu_seconds','wall_seconds','duration_ticks','cpu_per_unit_tick','cpu_per_building_tick','cpu_per_routing_query','admissions_per_tick','publications_per_tick','starvation_deaths','refresh_interval_mean_ticks',
        'memory_field_arrays_bytes','memory_search_queue_bytes','memory_shared_terrain_bytes','memory_pending_output_reservation_bytes')
    routing_metrics=('route_mean_time_penalty','route_p99_time_penalty','route_mean_extra_ticks','expanded_cells_per_tick')
    effectiveness={}
    identities=sorted({(score['ai'],score['team']) for g in games for score in g['scores']},key=str)
    for ai,seat in identities:
        observations=[]
        for game in games:
            value=next((s['trajectory'].get('score') for s in game['scores'] if s['ai']==ai and s['team']==seat),None)
            if value is not None:observations.append(dict(game,score=value))
        effectiveness[str(ai)+':seat'+str(seat)]=compare(observations,'score')
    report=dict(contract='routing-ratio-independent-analysis-v1',planned=len(source.manifest['jobs']),
        committed=sum(r['category']=='success' for r in records),terminal_records=len(records),observer_calibration=observer_overhead(source),failed_attempts=dict(Counter(r['category'] for r in source.attempts() if r['category']!='success')),
        missing_jobs=len(source.manifest['jobs'])-sum(r['category']=='success' for r in records),stages=stages,
        score_by_ai_seat=effectiveness,cpu_per_tick=compare(games),total_and_normalized_effects={m:compare(games,m) for m in metrics},
        routing_effects={m:compare(games,m,diagnostic=True) for m in routing_metrics},
        routing_by_generator={name:{m:compare([g for g in games if g['generator']==name],m,diagnostic=True) for m in routing_metrics}
            for name in sorted({g['generator'] for g in games})},high_diversity_cpu_descriptive=compare(games,'cpu_per_diverse_tick'),memory=compare(games,'peak_rss_bytes'),sampled_simulation_memory=compare(games,'simulation_sampled_rss_peak'),stages_cpu={s:compare(games,'cpu_per_tick_'+s) for s in ('early','mid','late')},adjusted=regression(games),
        by_generator={name:compare([g for g in games if g['generator']==name]) for name in sorted({g['generator'] for g in games})},games=games,
        limitations=['Routing traces follow fresh static fields, excluding congestion, changing goals and forbidden secondary ranking.',
            'Unified adaptive-quality errors are available only for preselected expensive-observer blocks; other diagnostic games still collect bounded routes, model inputs and work.',
            'Point-to-point searches have policy/reachability counters; frozen route-regret traces cover gradient-backed movement queries, not point A-star queries.',
            'Actual-speed oracle measures expected movement ticks with engine terrain/diagonal quantization; it excludes hazard utility and pickup penalties.',
            'Memory components include attached fields/queues and retained terrain plus output reservations; RSS also includes detached leases, pools, and other engine storage.',
            'Unit-tick and building-tick denominators are sampled population integrals, not exact per-tick censuses.',
            'Whole-game tick percentiles use the existing exact benchmark counters; stage percentiles have powers-of-two histogram bounds.',
            'Game-level confidence intervals do not treat queries, teams, or ticks as independent replications.',
            'Work collection starts after setup; initialization jobs reserved before observer attachment may not have kernel attribution.',
            'Refresh events and exact kernel work identify the shared field class; physical-class query counters describe who used it, without assigning shared work to one physical class.'])
    atomic_json(output,report)
    write_report(Path(output).with_suffix('.md'),report)
    return report


def write_report(path, report):
    lines=['# Movement-ratio sharing experiment', '',
        'Status: measured evidence for a maintainer decision; no policy has been promoted.', '',
        f"Completed games: {report['committed']} / {report['planned']}. Missing jobs: {report['missing_jobs']}.",
        '', '| Policy | CPU seconds/tick effect | 95% interval | Common setups |', '|---|---:|---|---:|']
    for arm,values in report['cpu_per_tick'].items():
        lines.append(f"| {arm} | {values['effect']} | {values['ci95']} | {values['setups']} |")
    lines.extend(['','Effects use independent seeds, equal setup weights, and randomized interleaving-block adjustment. Negative CPU effects favor sharing. Diagnostic CPU is excluded.','',
        '| Policy | Games | High-diversity team windows | Verified extra ratio slots | Capped route candidates |', '|---|---:|---:|---:|---:|'])
    for arm in sorted({g['arm'] for g in report['games']}):
        games=[g for g in report['games'] if g['arm']==arm]
        lines.append(f"| {arm} | {len(games)} | {sum(g['high_diversity_team_windows'] for g in games)} | {sum(g['confirmed_extra_ratio_slots'] for g in games)} | {sum(g['route_sampling_capped'] for g in games)} |")
    lines.extend(['', 'Inspect the JSON companion for map-specific effects, stage coverage, family work, memory, routing tails, adaptive guidance errors, adverse query identities, and AI/seat trajectory scores.', '',
        'A missing or wide interval is inconclusive. High-diversity subsets are descriptive post-treatment observations; the overall randomized cohort remains the primary comparison.', '', 'Measurement limits:', ''])
    lines.extend('- '+text for text in report['limitations'])
    path.write_text('\n'.join(lines)+'\n')


def finalists(report):
    invalid={g['arm'] for g in report['games'] if g.get('correctness_failures',0)}
    valid=[arm for arm in ARMS[1:] if arm not in invalid and report['cpu_per_tick'].get(arm,{}).get('effect') is not None]
    if len(valid)<2:raise ValueError('insufficient independent screening evidence to select two finalists')
    # Retain mode and use the fastest alternative on the observed CPU/quality frontier.
    costs={a:report['cpu_per_tick'][a]['effect'] for a in valid}
    quality={a:statistics.mean(g['route_time_penalty']['mean'] for g in report['games'] if g['arm']==a and g['route_time_penalty']['n'])
             for a in valid if any(g['arm']==a and g['route_time_penalty']['n'] for g in report['games'])}
    candidates=[a for a in valid if a!='mode' and a in quality]
    frontier=[a for a in candidates if not any(costs[b]<=costs[a] and quality[b]<=quality[a] and (costs[b]<costs[a] or quality[b]<quality[a]) for b in candidates if b!=a)]
    if not frontier:raise ValueError('missing route-quality evidence for screening finalists')
    chosen=min(frontier,key=lambda a:costs[a])
    if 'mode' in valid:return ['baseline','mode',chosen]
    alternatives=sorted(candidates,key=lambda a:(a not in frontier,costs[a]))
    if len(alternatives)<2:raise ValueError('fewer than two valid alternatives have route-quality evidence')
    return ['baseline',*alternatives[:2]]


def phase_job_counts(coordinator):
    """Admission only needs ledger states, not decoded historical game results."""
    return dict(coordinator.db.execute('SELECT state,count(*) FROM jobs GROUP BY state').fetchall())


def install_export_optimization():
    """Skip decoding immutable successful exports already present on disk.

    Only the coordinator process is changed; worker package identity stays frozen.
    Missing files and every non-success record retain the normal repair path.
    """
    def repair_exports(self):
        for row in self.db.execute('SELECT id,record,category FROM attempts WHERE record IS NOT NULL'):
            path=self.root/'attempts'/(row['id']+'.json')
            if row['category']=='success' and path.exists():continue
            record=json.loads(row['record'])
            if not path.exists():atomic_json(path,record)
            if record['category']!='success':
                path=self.root/'failures'/(row['id']+'.json')
                if not path.exists():atomic_json(path,record)
        for row in self.db.execute('SELECT j.id,a.record FROM jobs j JOIN attempts a ON j.accepted=a.id'):
            path=self.root/'results'/(row['id']+'.json')
            if not path.exists():atomic_json(path,json.loads(row['record']))
    Coordinator.repair_exports=repair_exports
    # Existing content-addressed objects need no new disk reservation or full
    # directory recount. The normal receiver still checks size and SHA-256.
    from tools.tournaments.transfer import receive_file
    from tools.tournaments.common import hash_id
    original=Coordinator.receive_artifact
    if not getattr(original,'ratio_existing_object_fast_path',False):
        def receive_artifact(self,transport,artifact):
            identity=hash_id(artifact['sha256']);size=artifact['bytes']
            if type(size) is not int or size<0:raise ValueError('invalid artifact byte size')
            if (self.root/'artifacts'/identity).exists():
                return receive_file(transport,self.root/'artifacts',identity,size,budget=8)
            return original(self,transport,artifact)
        receive_artifact.ratio_existing_object_fast_path=True
        Coordinator.receive_artifact=receive_artifact


def continuation_cpu_ids(root, receipt):
    """Explicitly amended capacity leaves the original campaign receipt intact."""
    amendment=Path(root)/'cpu-allocation-amendment.json'
    if not amendment.exists():return receipt['cpu_ids']
    record=read_json(amendment);ids=record['cpu_ids']
    if (record['original_cpu_ids']!=receipt['cpu_ids'] or
        record['original_deadline']!=receipt['deadline'] or
        len(set(ids))!=len(ids) or not set(receipt['cpu_ids'])<=set(ids) or len(ids)%4):
        raise ValueError('invalid CPU allocation amendment')
    return ids


def run_phase(root, bundle_path, manifest, cpu_ids, deadline):
    """Drain before the phase boundary; absolute per-job deadlines backstop it."""
    root=Path(root);phase=manifest['metadata']['phase'];directory=root/phase
    host=dict(name='ratio-local',transport='local',directory=str((root/'worker').resolve()),slots=3,
        cpu_ids=read_json(root/'campaign.json')['cpu_ids'],participants_per_game=4,collect_slots=1,prefetch=0,
        disk_reserve_bytes=10*GIB,spool_budget_bytes=4*GIB,cache_budget_bytes=4*GIB,
        attempt_budget_bytes=512*1024**2,log_budget_bytes=128*1024**2)
    host.pop('prefetch') # Coordinator-only setting.
    host_records=[host]
    extra=[c for c in cpu_ids if c not in host['cpu_ids']]
    if extra:
        host_records.append(dict(host,name='ratio-extra',directory=str((root/'worker-extra').resolve()),
            cpu_ids=extra,slots=len(extra)//4,collect_slots=2))
    hosts=root/'hosts.json';atomic_json(hosts,host_records)
    coordinator=Coordinator.submit(directory,manifest,[bundle_path]);frozen_manifest=coordinator.manifest;coordinator.close()
    log=(root/(phase+'-coordinator.log')).open('ab')
    runner=subprocess.Popen([sys.executable,'-c','from tools.gradient_ratio_experiment import install_export_optimization; install_export_optimization(); from tools.tournaments.__main__ import main; main()', 'run',str(directory),'--hosts',str(hosts)],stdout=log,stderr=subprocess.STDOUT)
    reason='complete';draining=False;last_monitor=0
    try:
        with gzip.open(root/(phase+'-host-'+str(time.time_ns())+'.jsonl.gz'),'wt') as monitor:
            while True:
                now=time.time();coordinator=Coordinator(directory,repair=False,manifest=frozen_manifest)
                try:
                    jobs=phase_job_counts(coordinator)
                    used=directory_bytes(root)
                    admission=read_json(root/'storage-admission.json') if (root/'storage-admission.json').exists() else {}
                    ceiling=admission.get('phase_ceiling_bytes',{}).get(phase,37*GIB)
                    if not 0<ceiling<=37*GIB:raise ValueError('invalid storage admission ceiling')
                    pressure=used>=ceiling or shutil.disk_usage(root).free<=12*GIB
                    if not draining and coordinator.mode=='draining':
                        draining=True;reason='explicit_drain'
                    if not draining and (now>=deadline-1815 or pressure):
                        reason='storage_guard' if pressure else 'phase_budget'
                        coordinator.control('draining');draining=True
                    if now-last_monitor>=30:
                        monitor.write(json.dumps(dict(time=now,load=os.getloadavg(),affinity=sorted(os.sched_getaffinity(0)),
                            cpu_pressure=Path('/proc/pressure/cpu').read_text() if Path('/proc/pressure/cpu').exists() else None,
                            cpu_times=[line for line in Path('/proc/stat').read_text().splitlines() if line.split()[0] in {'cpu'+str(c) for c in cpu_ids}] if Path('/proc/stat').exists() else None,
                            stored_bytes=used,free_bytes=shutil.disk_usage(root).free,jobs=jobs,reason=reason))+'\n')
                        monitor.flush();last_monitor=now
                    if not jobs.get('active',0) and (draining or not jobs.get('pending',0)):
                        coordinator.control('cancelled' if jobs.get('pending',0) else 'draining');break
                    if runner.poll() is not None:
                        if runner.returncode:raise RuntimeError('tournament coordinator failed; see '+str(log.name))
                        break
                finally:coordinator.close()
                # Every game has the immutable phase deadline. No new work is
                # admitted while draining, including retries and packing work.
                time.sleep(2)
    finally:
        if runner.poll() is None:
            runner.terminate()
            try:runner.wait(timeout=15)
            except subprocess.TimeoutExpired:runner.kill();runner.wait()
        log.close()
    atomic_json(root/(phase+'-stop.json'),dict(reason=reason,deadline=deadline,finished=time.time()))
    return directory


def _campaign(bundle_path, directory, cpu_ids, hours=48):
    if len(cpu_ids)!=12 or len(set(cpu_ids))!=12 or not set(cpu_ids)<=os.sched_getaffinity(0):
        raise ValueError('campaign requires exactly 12 distinct currently allowed logical CPUs')
    if not 0<hours<=48:raise ValueError('campaign duration must be in (0,48] hours')
    os.sched_setaffinity(0,cpu_ids)
    root=Path(directory).resolve();root.mkdir(parents=True,exist_ok=True)
    receipt=root/'campaign.json'
    if receipt.exists():raise ValueError('campaign directory already has a receipt; preserve it and use a new directory')
    bundle=inspect_bundle(bundle_path)
    if bundle['capabilities'].get('save_version',0)<195 or bundle['build_options'].get('routing_ratio_contract')!='routing-ratio-study-v1':
        raise ValueError('campaign requires a validated format-195 research release bundle with routing_ratio_contract attestation')
    started=time.time();end=started+hours*3600
    # Keep final analysis inside the campaign budget. Worker deadlines are
    # earlier; no simulation work is admitted into this closing reserve.
    work_end=end-min(900,hours*3600*.1)
    atomic_json(receipt,dict(contract='routing-ratio-campaign-v1',started=started,deadline=end,cpu_ids=cpu_ids,
        bundle=bundle['id'],simulation_version=bundle['capabilities'].get('sim_version'),
        model_sha256=bundle['build_options'].get('adaptive_model_sha256'),slots=3,participants=4))
    pilot_end=min(end,started+6*3600)
    pilot=add_observer_calibration(plan(bundle,phase='pilot',deadline=pilot_end))
    pilot_dir=run_phase(root,bundle_path,pilot,cpu_ids,pilot_end)
    pilot_report=summarize(pilot_dir,root/'pilot-report.json')
    _finish_campaign(root,bundle_path,bundle,cpu_ids,pilot_report,started,end,work_end)


def _resume_pilot(bundle_path,directory,cpu_ids):
    root=Path(directory).resolve();receipt=read_json(root/'campaign.json')
    if cpu_ids!=receipt['cpu_ids']:raise ValueError('resume must preserve the original CPU allocation')
    os.sched_setaffinity(0,cpu_ids)
    started,end=receipt['started'],receipt['deadline'];pilot_end=min(end,started+6*3600)
    if time.time()>=pilot_end-1815:raise ValueError('original pilot admission window has expired')
    if (root/'stages.json').exists():raise ValueError('pilot recovery is unavailable after stage freezing')
    manifest=read_json(root/'pilot/experiment.json');manifest.pop('package_id',None)
    original_id=manifest['id'];manifest['id']+='-recording-v2';manifest['metadata']['phase']='pilot-recovery'
    manifest['metadata']['recording_amendment']='Retain required gameplay/probability/performance logs; omit verbose per-AI implementation samples. Simulation inputs unchanged. Original cohort superseded, never pooled.'
    amended=[]
    for old in manifest['jobs']:
        outputs=dict(old['outputs'],log_mode='research')
        amended.append(job(old['type'],old['build'],inputs=old['inputs'],seeds=old['seeds'],config=old['config'],outputs=outputs,
            limits=dict(old['limits'],log_bytes=128*1024**2),labels=dict(old['labels'],supersedes_job=old['id'],recording_revision=2),depends_on=old['depends_on']))
    manifest['jobs']=amended
    atomic_json(root/'recording-amendment.json',dict(original_cohort=original_id,replacement_cohort=manifest['id'],
        reason='Verbose AI samples exceeded the decoded log cap. Explicit research filtering retains requested streams, with a 128 MiB decoded-log allowance inside the unchanged 512 MiB attempt cap.',
        original_started=started,original_deadline=end,applied=time.time(),superseded_results='pilot',primary_results='pilot-recovery',
        unchanged=['executable','policies','seeds','model','horizon','stopping rule','CPU allocation','per-attempt and campaign storage limits']))
    bundle=inspect_bundle(bundle_path)
    pilot_dir=run_phase(root,bundle_path,manifest,cpu_ids,pilot_end)
    pilot_report=summarize(pilot_dir,root/'pilot-report.json')
    pilot_report['superseded_cohort']='pilot; retained as recording-repair evidence, excluded from policy comparisons'
    atomic_json(root/'pilot-report.json',pilot_report)
    _finish_campaign(root,bundle_path,bundle,cpu_ids,pilot_report,started,end,end-min(900,(end-started)*.1))


def _continue_pilot(bundle_path,directory,cpu_ids):
    """Reattach orchestration without changing any admitted or pending pilot job."""
    root=Path(directory).resolve();receipt=read_json(root/'campaign.json')
    if cpu_ids!=continuation_cpu_ids(root,receipt):raise ValueError('continuation must match approved CPU allocation')
    if time.time()>=receipt['deadline']:raise ValueError('campaign continuation window is unavailable')
    os.sched_setaffinity(0,cpu_ids)
    manifest=read_json(root/'pilot-recovery/experiment.json')
    coordinator=Coordinator(root/'pilot-recovery')
    try:
        status=coordinator.status()
        finished=not status['jobs'].get('active',0) and not status['jobs'].get('pending',0)
        if not finished:coordinator.control('running')
    finally:coordinator.close()
    bundle=inspect_bundle(bundle_path)
    pilot_dir=root/'pilot-recovery' if finished else run_phase(root,bundle_path,manifest,cpu_ids,min(receipt['deadline'],receipt['started']+6*3600))
    report=read_json(root/'pilot-report.json') if (root/'pilot-report.json').exists() else summarize(pilot_dir,root/'pilot-report.json')
    report['superseded_cohort']='pilot; recording repair only, excluded from comparisons'
    atomic_json(root/'pilot-report.json',report)
    _finish_campaign(root,bundle_path,bundle,cpu_ids,report,receipt['started'],receipt['deadline'],receipt['deadline']-900)


def diagnostic_projection(root,pilot_report,quality_fraction=.2):
    """Use all observed attempt costs, including censored diagnostic timeouts."""
    light=[g['wall_seconds'] for g in pilot_report['games'] if not g['diagnostic']]
    if not light:raise ValueError('no lightweight throughput evidence')
    light_seconds=statistics.median(light)
    source=Results(Path(root)/('pilot-recovery' if (Path(root)/'pilot-recovery').exists() else 'pilot'))
    heavy=[r['finished']-r['started'] for r in source.attempts()
        if r.get('finished') and r.get('job',{}).get('labels',{}).get('diagnostic')
        and 'observer_mode' not in r['job']['labels'] and r['category'] in ('success','timeout')]
    heavy_seconds=quantile(heavy,.75) if heavy else light_seconds*10
    # Conservative projection for bounded route/kernel diagnostics; actual
    # throughput will be reported separately, never presented as measured here.
    route_seconds=2*light_seconds
    mixed=quality_fraction*heavy_seconds+(1-quality_fraction)*route_seconds
    diagnostic_fraction=(.2*light_seconds)/(.8*mixed+.2*light_seconds)
    return dict(light_seconds=light_seconds,heavy_seconds=heavy_seconds,heavy_attempts=len(heavy),
        bounded_route_seconds_projection=route_seconds,mixed_diagnostic_seconds_projection=mixed,
        quality_probe_fraction=quality_fraction,screen_diagnostic_job_fraction=diagnostic_fraction,
        interpretation='Measured lightweight median and diagnostic attempt p75 include timeouts; bounded-route cost is a conservative projection.')


def drain_supplemental(root,bundle_path,cpu_ids):
    root=Path(root);manifest=read_json(root/'confirmation/experiment.json')
    coordinator=Coordinator(root/'confirmation',repair=False)
    try:
        if coordinator.mode not in ('draining','cancelled'):
            raise ValueError('supplemental cohort must have admission stopped before parallel collection')
        jobs=phase_job_counts(coordinator)
        finished=not jobs.get('active',0) and not jobs.get('pending',0)
    finally:coordinator.close()
    directory=root/'confirmation' if finished else run_phase(root,bundle_path,manifest,cpu_ids,manifest['jobs'][0]['limits']['deadline_unix'])
    if not (root/'confirmation-report.json').exists():summarize(directory,root/'confirmation-report.json',read_json(root/'stages.json'))


def start_supplemental_drain(root,bundle_path,cpu_ids):
    """Only the primary cohort admits games; the old cohort collects results."""
    root=Path(root)
    log=(root/'supplemental-drain.log').open('ab')
    command=[sys.executable,'-m','tools.gradient_ratio_experiment','drain-supplemental',
        '--directory',str(root),'--bundle',str(bundle_path),'--cpu-ids',','.join(map(str,cpu_ids))]
    try:process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
    finally:log.close()
    atomic_json(root/'supplemental-drain-launch.json',dict(pid=process.pid,started=time.time(),command=command,
        role='collect and summarize existing original-cohort results; no admission'))
    return process


def _finish_campaign(root,bundle_path,bundle,cpu_ids,pilot_report,started,end,work_end):
    if not any(g['arm']=='baseline' and g['high_diversity_with_extra_builds'] for g in pilot_report['games']):
        raise RuntimeError('Pilot did not verify diverse amphibious query demand with duplicated ratio builds; revise workload coverage within the original remaining budget before screening')
    stages=pilot_report['stages']
    if (root/'stages.json').exists():
        if read_json(root/'stages.json')!=stages:raise ValueError('frozen stage definitions changed')
    else:atomic_json(root/'stages.json',stages)
    durations=[g['wall_seconds'] for g in pilot_report['games'] if not g['diagnostic']]
    if not durations:raise RuntimeError('pilot produced no valid lightweight games')
    projection=diagnostic_projection(root,pilot_report)
    atomic_json(root/'diagnostic-budget-projection.json',projection)
    median=projection['light_seconds']
    screen_hours=min(10*3600,max(0,end-time.time())*.3)
    count=int(3*screen_hours*.8*.85/median/(len(ARMS)*4));setups=min(120,max(2,count-count%2))
    screen_finished=False
    if (root/'screening/experiment.json').exists():
        screen=read_json(root/'screening/experiment.json')
        screen_end=screen['jobs'][0]['limits']['deadline_unix']
        coordinator=Coordinator(root/'screening')
        try:
            status=coordinator.status()
            screen_finished=not status['jobs'].get('active',0) and not status['jobs'].get('pending',0)
            if not screen_finished:coordinator.control('running')
        finally:coordinator.close()
    else:
        screen_end=min(end,time.time()+screen_hours)
        screen=plan(bundle,phase='screening',setups=setups,seed=20261103,deadline=screen_end,
            diagnostics=projection['screen_diagnostic_job_fraction'],quality_fraction=.2,process_attempts=1)
    screen_dir=root/'screening' if screen_finished else run_phase(root,bundle_path,screen,cpu_ids,screen_end)
    screen_report=read_json(root/'screening-report.json') if (root/'screening-report.json').exists() else summarize(screen_dir,root/'screening-report.json',stages)
    chosen=finalists(screen_report);atomic_json(root/'finalists.json',dict(arms=chosen,rule='retain mode and fastest measured CPU/route-quality Pareto alternative',source=digest(screen_report)))
    remaining=work_end-time.time()
    if remaining<=3600 and not (root/'confirmation/experiment.json').exists():raise RuntimeError('insufficient remaining campaign time for confirmation')
    def continue_phase(name,manifest,deadline):
        directory=root/name
        if (directory/'state.sqlite').exists():
            coordinator=Coordinator(directory,repair=False)
            try:
                jobs=phase_job_counts(coordinator)
                finished=not jobs.get('active',0) and not jobs.get('pending',0)
                if not finished and coordinator.mode=='paused':coordinator.control('running')
            finally:coordinator.close()
            if finished:return directory
        return run_phase(root,bundle_path,manifest,cpu_ids,deadline)
    amendment=read_json(root/'confirmation-amendment.json') if (root/'confirmation-amendment.json').exists() else None
    primary='confirmation';diagnostic_name='diagnostic-confirmation';supplemental=None
    if amendment:
        if amendment['original_deadline']!=end or amendment['screening_source']!=digest(screen_report):
            raise ValueError('confirmation amendment changed campaign deadline or screening source')
        if amendment['arms']!=['baseline','mode','median']:
            raise ValueError('unsupported confirmation amendment arms')
        # The original cohort is drained and summarized separately. Never pool
        # its selected arms or time blocks into the fresh amended comparison.
        if not (root/'confirmation-report.json').exists():supplemental=start_supplemental_drain(root,bundle_path,cpu_ids)
        chosen=amendment['arms'];primary='confirmation-median';diagnostic_name='diagnostic-confirmation-median'
        confirmation=read_json(root/amendment['performance_manifest'])
        diagnostic=read_json(root/amendment['diagnostic_manifest'])
        for name,value in [(primary,confirmation),(diagnostic_name,diagnostic)]:
            if value['metadata']['phase']!=name or {j['labels']['variant'] for j in value['jobs']}!=set(chosen):
                raise ValueError('amended manifest has wrong phase or arms')
    elif (root/'confirmation/experiment.json').exists():
        confirmation=read_json(root/'confirmation/experiment.json')
    else:
        perf_end=time.time()+remaining*.8
        count=int(3*(perf_end-time.time())*.85/median/(len(chosen)*4));setups=min(300,max(2,count-count%2))
        confirmation=plan(bundle,phase='confirmation',setups=setups,seed=20261104,arms=chosen,diagnostics=0,deadline=perf_end,process_attempts=1)
    perf_end=confirmation['jobs'][0]['limits']['deadline_unix']
    confirm_dir=continue_phase(primary,confirmation,perf_end)
    if not (root/(primary+'-report.json')).exists():summarize(confirm_dir,root/(primary+'-report.json'),stages)
    if not amendment and (root/'diagnostic-confirmation/experiment.json').exists():
        diagnostic=read_json(root/'diagnostic-confirmation/experiment.json')
    elif not amendment:
        diagnostic_median=projection['mixed_diagnostic_seconds_projection']
        count=int(3*max(0,work_end-time.time())*.75/diagnostic_median/(len(chosen)*4));setups=min(120,max(2,count-count%2))
        affordable=int((len(cpu_ids)//4)*max(0,work_end-time.time())*.75/diagnostic_median/(len(chosen)*4))
        if affordable>=12:setups=max(setups,12)
        diagnostic=plan(bundle,phase='confirmation',setups=setups,seed=20261105,arms=chosen,diagnostics=1,deadline=work_end,quality_fraction=.2,process_attempts=1)
        diagnostic['metadata']['phase']='diagnostic-confirmation'
        diagnostic['id']+='-diagnostic'
    diagnostic_end=diagnostic['jobs'][0]['limits']['deadline_unix']
    diagnostic_dir=continue_phase(diagnostic_name,diagnostic,diagnostic_end)
    if not (root/(diagnostic_name+'-report.json')).exists():summarize(diagnostic_dir,root/(diagnostic_name+'-report.json'),stages)
    if supplemental:
        if supplemental.wait(timeout=max(1,end-time.time()-30)):
            raise RuntimeError('supplemental result collection failed; see supplemental-drain.log')
    atomic_json(root/'completed.json',dict(finished=time.time(),started=started,arms=chosen,
        decision='Evidence ready for maintainer decision; no production policy changed'))


def decision_package(root, error=None):
    root=Path(root)
    reports={name:read_json(root/(name+'-report.json')) for name in
        ('pilot','screening','confirmation','diagnostic-confirmation','confirmation-median','diagnostic-confirmation-median') if (root/(name+'-report.json')).exists()}
    amended=(root/'confirmation-amendment.json').exists()
    package=dict(status='attention_required' if error else 'complete',error=error,
        reports={name:name+'-report.json' for name in reports},production_policy='baseline',
        platform_coverage='Native Linux x86_64 validated; Windows, macOS, browser and mobile execution unverified')
    lines=['# Movement-ratio decision package','',
        'Status: '+('stopped with incomplete evidence: '+error if error else 'campaign complete; no production policy promoted'),'',
        'Policy comparisons use independent game seeds. Screening is exploratory; confirmation is held out. Observer calibration uses identical seeds only to isolate instrumentation cost.','']
    for name,report in reports.items():
        role=' — supplemental, collected before finalist amendment' if amended and name=='confirmation' else ''
        lines.extend([f"## {name}{role}",'',f"Successful games: {report['committed']} / {report['planned']}; missing/failed: {report['missing_jobs']}.",'',
            '| Policy | CPU seconds per tick difference | 95% CI | Common setups |','|---|---:|---|---:|'])
        for arm,value in report['cpu_per_tick'].items():
            lines.append(f"| {arm} | {value['effect']} | {value['ci95']} | {value['setups']} |")
        lines.extend(['','Map-specific CPU effects:','', '| Map | Policy | Difference | 95% CI |','|---|---|---:|---|'])
        for generator,arms in report['by_generator'].items():
            for arm,value in arms.items():lines.append(f"| {generator} | {arm} | {value['effect']} | {value['ci95']} |")
        lines.extend(['','Routing estimates (game means summarized here; query tails and confidence intervals are in the JSON report):','',
            '| Policy | Queries | Mean game travel-time penalty | p95 of game means |','|---|---:|---:|---:|'])
        for arm in ARMS:
            games=[g for g in report['games'] if g['arm']==arm and g['diagnostic']]
            values=describe([g['route_time_penalty']['mean'] for g in games])
            lines.append(f"| {arm} | {sum(g['route_time_penalty']['n'] for g in games)} | {values['mean']} | {values['p95']} |")
    primary='confirmation-median' if (root/'confirmation-amendment.json').exists() else 'confirmation'
    package['primary_confirmation']=primary
    package['supplemental_confirmation']='confirmation' if primary!='confirmation' else None
    confirm=reports.get(primary,{})
    ranking=sorted((v['effect'],a) for a,v in confirm.get('cpu_per_tick',{}).items() if v['effect'] is not None)
    package['cpu_ranking']=[a for _,a in ranking]
    lines.extend(['','Measured CPU ranking: '+(', '.join(package['cpu_ranking']) or 'unavailable'),'',
        'Decision: retain the production baseline pending maintainer review of CPU intervals, memory, routing tails, gameplay and adaptive errors. A CPU ranking alone does not settle the trade-off. Missing/wide intervals and absent diverse workloads remain inconclusive.','',
        package['platform_coverage'], '', 'Frozen manifests, compressed logs, telemetry and bounded diagnostic saves/replays remain in the phase tournament directories. See each report JSON for per-game AI/seat trajectory scores and adverse-route identities.'])
    atomic_json(root/'decision-package.json',package)
    (root/'decision-package.md').write_text('\n'.join(lines)+'\n')


def campaign(bundle_path, directory, cpu_ids, hours=48, resume_pilot=False, continue_pilot=False):
    try:
        if continue_pilot:_continue_pilot(bundle_path,directory,cpu_ids)
        elif resume_pilot:_resume_pilot(bundle_path,directory,cpu_ids)
        else:_campaign(bundle_path,directory,cpu_ids,hours)
    except Exception as error:
        if Path(directory).is_dir():
            atomic_json(Path(directory)/'attention.json',dict(error=str(error),time=time.time()))
            decision_package(directory,str(error))
        raise
    finally:
        for worker in Path(directory).glob('worker*/workers/*'):
            if worker.is_dir():atomic_json(worker/'stop.json',dict(requested=time.time()))
    decision_package(directory)


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('plan');p.add_argument('--bundle',required=True);p.add_argument('--output',required=True)
    p.add_argument('--phase',choices=('pilot','screening','confirmation'),required=True);p.add_argument('--setups',type=int,default=12)
    p.add_argument('--replicates',type=int,default=4);p.add_argument('--seed',type=int,default=20261102);p.add_argument('--arms',nargs='+',default=list(ARMS))
    p.add_argument('--ticks',type=int,default=90000);p.add_argument('--diagnostics',type=float,default=.2)
    p=sub.add_parser('analyze');p.add_argument('directory');p.add_argument('--output',required=True);p.add_argument('--stages')
    p=sub.add_parser('freeze-stages');p.add_argument('directory');p.add_argument('--output',required=True)
    p=sub.add_parser('drain-supplemental');p.add_argument('--bundle',required=True);p.add_argument('--directory',required=True);p.add_argument('--cpu-ids',required=True)
    p=sub.add_parser('campaign');p.add_argument('--bundle',required=True);p.add_argument('--directory',required=True)
    p.add_argument('--cpu-ids',required=True);p.add_argument('--hours',type=float,default=48);p.add_argument('--resume-pilot',action='store_true');p.add_argument('--continue-pilot',action='store_true')
    args=parser.parse_args()
    if args.command=='plan':atomic_json(args.output,plan(inspect_bundle(args.bundle),phase=args.phase,setups=args.setups,replicates=args.replicates,seed=args.seed,arms=args.arms,ticks=args.ticks,diagnostics=args.diagnostics))
    elif args.command=='drain-supplemental':drain_supplemental(args.directory,args.bundle,[int(v) for v in args.cpu_ids.split(',')])
    elif args.command=='campaign':campaign(args.bundle,args.directory,[int(v) for v in args.cpu_ids.split(',')],args.hours,args.resume_pilot,args.continue_pilot)
    elif args.command=='freeze-stages':atomic_json(args.output,freeze_stages(Results(args.directory)))
    else:summarize(args.directory,args.output,read_json(args.stages) if args.stages else None)

if __name__=='__main__':main()
