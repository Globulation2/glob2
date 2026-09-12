#!/usr/bin/env python3
"""Frozen four-opponent protocol, scenario identities, receipts and qualification gates."""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import random
import re
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
FORMATS={'duel':2,'ffa3':3,'ffa4':4,'ffa5':5,'2v2':4}
OPPONENTS={1:'AINumbi',2:'AICastor',5:'AIEcho::Echo/NewNicowar',6:'AIMaxima::Maxima'}
CHECKPOINTS=(200000,)
NO_ORDERS='_experiment.no_orders'
NAMESPACES={'controls':1,'pilot':2,'confirmation':3,'combination':4,'qualification':5}
HOSTS={'pharaoh-dev-1.local':3,'pharaoh-dev-2.local':3,'pharaoh-dev-3.local':3,
       'devlaptop.local':15,'therig.local':31}
REQUIRED_GATES=('routing','opponent_identity','player_isolation','save_load',
                'behavioral','reproducibility_across_hosts','positive_control',
                'statistical_sensitivity')


def canonical(value): return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False)
def identity(value): return hashlib.sha256(canonical(value).encode()).hexdigest()
def sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576),b''): digest.update(chunk)
    return digest.hexdigest()
def atomic(path,value):
    path=Path(path); path.parent.mkdir(parents=True,exist_ok=True)
    pending=path.with_suffix(path.suffix+'.pending')
    with pending.open('w') as stream:
        stream.write(json.dumps(value,indent=2,allow_nan=False)+'\n'); stream.flush(); os.fsync(stream.fileno())
    pending.replace(path)
def clean_environment():
    env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_MAXIMA','GLOB2_NICOWAR_V3'))}
    env.update(OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1')
    return env


def binary_json(binary,*args):
    return json.loads(subprocess.check_output([str(binary),*args],cwd=ROOT,env=clean_environment(),text=True))


def inventory(binary):
    schema=binary_json(binary,'--dump-maxima-schema')
    defaults={fmt:{p['key']:p['value'] for p in binary_json(binary,'--dump-maxima-strategy',
                     '--maxima-format','ffa5plus' if fmt=='ffa5' else fmt)['parameters']}
              for fmt in FORMATS}
    members={key:(group,member) for group,member,key in re.findall(
        r'BOOL_SPEC\((\w+),\s*(\w+),\s*"([^"]+)"',(ROOT/'src/AIMaximaStrategy.cpp').read_text())}
    matrix=[]
    for parameter in schema['parameters']:
        if parameter['type']!='boolean': continue
        key=parameter['key']; group,member=members[key]
        consumers=[]
        for path in sorted((ROOT/'src').glob('AIMaxima*')):
            if path.suffix not in ('.cpp','.h') or path.name in ('AIMaximaStrategy.cpp','AIMaximaStrategy.h'): continue
            for number,line in enumerate(path.read_text().splitlines(),1):
                if re.search(r'\b'+re.escape(member)+r'\b',line):
                    consumers.append({'path':str(path.relative_to(ROOT)),'line':number,'text':line.strip()})
        parents=[]
        if key.startswith('farming.') and key!='farming.enabled': parents=['farming.enabled']
        if key.startswith('recon.') and key!='recon.enabled': parents=['recon.enabled']
        if key.startswith('tactics.') and key!='tactics.enabled': parents=['tactics.enabled']
        if key=='military.preemptive_amphibious_enabled': parents=['military.preemptive_defense_enabled']
        matrix.append({'switch':key,'member':f'{group}.{member}','aliases':[],
                       'defaults':{fmt:defaults[fmt][key] for fmt in FORMATS},
                       'parents':parents,'dependency_status':'requires behavioral audit',
                       'consumer_candidates':consumers,'validated':False,
                       'fixtures':{'eligible':None,'ineligible':None,
                                   'parent_disabled':None if parents else 'not_applicable'},
                       'status':'unvalidated: consumer locations are not behavioral evidence'})
    return schema,defaults,matrix


def prepare(binary,out):
    out.mkdir(parents=True,exist_ok=True)
    if (out/'protocol.json').exists(): raise ValueError('protocol already exists; create a new campaign directory')
    schema,defaults,matrix=inventory(binary)
    text=subprocess.check_output([str(binary),'-list-nicowar-scenario-maps'],cwd=ROOT,
                                 env=clean_environment(),text=True)
    maps=[]
    for line in text.splitlines():
        if line.startswith('NICOWAR_SCENARIO_MAP\t'):
            _,path,name,teams=line.split('\t')
            maps.append({'path':path,'name':name,'teams':int(teams),'sha256':sha(ROOT/path)})
    by_format={fmt:[m for m in maps if m['teams']==4 if fmt=='2v2'] if fmt=='2v2'
               else [m for m in maps if m['teams']>=players] for fmt,players in FORMATS.items()}
    if any(not v for v in by_format.values()): raise ValueError('empty map distribution')
    protocol={'schema':1,'status':'qualification_required','binary':str(binary.resolve()),
              'binary_sha256':sha(binary),'registry':schema,'defaults':defaults,
              'switches':[m['switch'] for m in matrix], 'maps':by_format,
              'benchmark':[{'format':f,'opponent':o,'weight':.05} for f in FORMATS for o in OPPONENTS],
              'map_distribution':'uniform within format','seat_distribution':'uniform over valid seats/offsets; all six 2v2 sides',
              'execution_mode':'uninterrupted','automatic_checkpoint_resumption':False,
              'hard_tick_limit':200000,'unfinished_at_limit':'population tiebreaker; equal leaders draw',
              'cutoff_scoring':'population_v1',
              'estimand':'probability of a sole tournament win (natural or population cutoff); draws score zero',
              'scenario_index_offset':20000,
              'checkpoint_harvest_interval':200000,
              'checkpoints':CHECKPOINTS,'controls':{'pairs':200,'per_cell':10,'order':[1,2,5,7],
                   'kind':'no_orders','seed_namespace':8,'scope':'all focal players; AI getOrder bypassed; world simulation continues',
                   'positive_control_alpha':.05,'require_interval_excludes_zero':True},
              'pilot_pairs_per_switch':100,'hypothesis_alpha':.04/len(matrix),'combination_alpha':.01,
              'target_effect':.02,'target_power':.90,'repeat_probability':.05,'hosts':HOSTS,
              'previous_results':'exploratory; never pool','farming_parents_enabled':True,
              'confirmation_requires_measured_compute_allocation':True}
    files=[*[p for p in sorted((ROOT/'src').rglob('*.*')) if p.suffix in ('.cpp','.h')],*sorted((ROOT/'data').rglob('*')),
           ROOT/'tools/maxima_win_experiment.py',ROOT/'tools/maxima_win_statistics.py',
           ROOT/'tools/maxima_experiment_queue.py', ROOT/'tools/maxima_experiment_fleet.py', ROOT/'tools/maxima_win_report.py', ROOT/'tools/qualify_maxima_win_experiment.py', ROOT/'tools/qualify_maxima_long_continuation.py', ROOT/'tools/qualify_maxima_no_orders.py', ROOT/'tools/qualify_maxima_tick_limit.py', ROOT/'tools/qualify_maxima_population_scoring.py',
           ROOT/'test/MaximaCombatIntegrationTest.cpp', ROOT/'test/MaximaImplementationIntegrationTest.cpp',
           ROOT/'test/TrappedUnitLifecycleTest.cpp',
           ROOT/'test/ClearingFlagGradientTest.cpp',
           ROOT/'test/SwarmSurvivalTest.cpp',
           *[ROOT/'test'/name for name in ('MaximaAdditionalSwitchBehaviorTest.cpp','MaximaPreemptiveSwitchBehaviorTest.cpp')],
           *[ROOT/'tools'/name for name in ('qualify_maxima_additional_behavior.py','qualify_maxima_preemptive_behavior.py')],
           ROOT/'test/MaximaFleetTest.py',
           ROOT/'test/MaximaWinExperimentTest.py', ROOT/'test/MaximaAuditReceiptTest.py', ROOT/'requirements-maxima-experiment.txt']
    protocol['runtime_and_analysis']={str(p.relative_to(ROOT)):sha(p) for p in files if p.is_file()}
    protocol['protocol_id']=identity(protocol)
    atomic(out/'protocol.json',protocol); atomic(out/'audit-matrix.json',matrix)
    import tarfile
    with tarfile.open(out/'qualification-inputs.tar.gz','w:gz') as bundle:
        bundle.add(binary,arcname='binary/glob2',recursive=False)
        for name in protocol['runtime_and_analysis']:
            bundle.add(ROOT/name,arcname=name,recursive=False)
        for name in sorted({m['path'] for maps in by_format.values() for m in maps}):
            bundle.add(ROOT/name,arcname=name,recursive=False)
        bundle.add(out/'protocol.json',arcname='protocol.json',recursive=False)
    atomic(out/'qualification-inputs.sha256.json',{'sha256':sha(out/'qualification-inputs.tar.gz'),'status':'candidate; not a repaired qualified freeze'})
    lines=['# Maxima switch audit matrix','',
           'All entries start unvalidated. Configuration receipts and source matches do not establish behavior.','',
           '| Switch | Defaults (duel/FFA3/FFA4/FFA5/2v2) | Parent candidates | Fixtures |',
           '|---|---|---|---|']
    for m in matrix:
        lines.append('| '+m['switch']+' | '+ '/'.join(str(v).lower() for v in m['defaults'].values())+
                     ' | '+(', '.join(m['parents']) or 'none identified')+' | unvalidated |')
    (out/'AUDIT_MATRIX.md').write_text('\n'.join(lines)+'\n')
    atomic(out/'controls-scenarios.json',[scenario(protocol,'controls',i,balanced=True) for i in range(200)])
    return protocol


def scenario(protocol,stage,index,balanced=False):
    if stage not in NAMESPACES or not 0<=index<2**28: raise ValueError('invalid independent scenario index')
    namespace=protocol.get('controls',{}).get('seed_namespace',NAMESPACES[stage]) if stage=='controls' else NAMESPACES[stage]
    seed=namespace*2**28+index+protocol.get('scenario_index_offset',0)
    rng=random.Random(identity({'protocol':protocol['protocol_id'],'seed':seed}))
    if balanced:
        if stage!='controls' or index>=200: raise ValueError('balanced allocation is only for 200 controls')
        # 100 Numbi/Castor then 100 Nicowar/Maxima, ten pairs per cell.
        opponent=(1,2,5,7)[index//50]; fmt=tuple(FORMATS)[(index%50)//10]
    else:
        fmt=rng.choice(tuple(FORMATS)); opponent=rng.choice(tuple(OPPONENTS))
    map_info=rng.choice(protocol['maps'][fmt]); count=FORMATS[fmt]
    seat=rng.randrange(count); offset=rng.randrange(map_info['teams'])
    partition=rng.randrange(3); swap=rng.randrange(2)
    if fmt=='2v2':
        side={0,partition+1}; side=set(range(4))-side if swap else side
        players=[{'player':i,'team':i,'focal':i in side} for i in range(4)]
    else:
        players=[{'player':i,'team':(offset+i*map_info['teams']//count)%map_info['teams'],
                  'focal':i==seat} for i in range(count)]
    value={'stage':stage,'index':index,'seed':seed,'format':fmt,'opponent':opponent,
           'map':map_info,'seat':seat,'offset':offset,'partition':partition,'swap':swap,'players':players,
           'order':rng.sample(['on','off'],2),'repeat':rng.random()<.05}
    value['scenario_id']=identity(value)
    return value


def arm_settings(protocol,scenario,switch=None,on=True):
    baseline=dict(protocol['defaults'][scenario['format']])
    # Frozen experimental reference differs from shipped farming defaults where needed.
    baseline['farming.enabled']=True
    result={}
    for p in scenario['players']:
        if p['focal'] or scenario['opponent']==7:
            values=dict(baseline)
            if switch and p['focal']:
                if switch not in protocol['switches']: raise ValueError('unregistered switch')
                values[switch]=on
            result[str(p['player'])]=values
    return result


def command(protocol,scenario,settings,cap,audit,checkpoint=None,save=None):
    if cap>protocol.get('hard_tick_limit',cap): raise ValueError('requested cap exceeds hard tick limit')
    binary=protocol['binary']
    if checkpoint:
        path,tick=checkpoint
        args=[binary,'--maxima-checkpoint-run',str(path),str(cap-tick)]
    elif scenario['format']=='2v2':
        args=[binary,'-nicowar-2v2-match-nox',str(ROOT/scenario['map']['path']),str(scenario['seed']),
              '7',str(scenario['opponent']),str(scenario['partition']),str(scenario['swap']),str(cap)]
    else:
        args=[binary,'-nicowar-scenario-match-nox',str(ROOT/scenario['map']['path']),str(scenario['seed']),
              str(FORMATS[scenario['format']]),'7',str(scenario['opponent']),str(scenario['seat']),
              str(scenario['offset']),str(cap)]
    for player,values in sorted(settings.items()):
        if values.get(NO_ORDERS,False): args+=['--maxima-no-orders-player',player]
        args+=['--maxima-player-overrides',player,','.join(k+'='+str(v).lower() for k,v in sorted(values.items()) if k!=NO_ORDERS)]
    envelope={'binary':protocol['binary_sha256'],'protocol':protocol['protocol_id'],
              'scenario':scenario['scenario_id'],'initial_state':sha(checkpoint[0]) if checkpoint else scenario['map']['sha256'],
              'configuration':identity(settings)}
    args+=['--maxima-audit',str(Path(audit).resolve()),canonical(envelope)]
    if save: args+=['--maxima-checkpoint-save',str(Path(save).resolve()),str(cap)]
    return args,envelope


class IntegrityError(RuntimeError): pass

def read_audit(path,envelope,scenario,settings):
    try:
        with Path(path).open() as stream: records=[json.loads(line) for line in stream]
        if [r['sequence'] for r in records]!=list(range(len(records))): raise IntegrityError('missing or reordered audit record')
        if any(r['schema']!=1 for r in records): raise IntegrityError('unsupported audit schema')
        if records[0]['type']!='identity' or json.loads(records[0]['identities'])!=envelope: raise IntegrityError('identity mismatch')
        starts=[r for r in records if r['type']=='start']; ends=[r for r in records if r['type']=='terminal']
        if len(starts)!=1 or len(ends)!=1 or records[-1]!=ends[0]: raise IntegrityError('missing/duplicate boundary receipts')
        if any(r['type']!='initialization' for r in records[1:records.index(starts[0])]): raise IntegrityError('decision/order before treatment receipt')
        expected={p['player']:p for p in scenario['players']}
        for boundary in (starts[0],ends[0]):
            if Counter(p['player'] for p in boundary['players'])!=Counter({i:1 for i in expected}): raise IntegrityError('player receipt mismatch')
            for receipt in boundary['players']:
                p=expected[receipt['player']]; ai=7 if p['focal'] else scenario['opponent']
                if receipt['team']!=p['team'] or receipt['ai_id']!=ai or receipt['implementation']!=OPPONENTS[ai]: raise IntegrityError('player/opponent routing mismatch')
                allies=sum(1<<other['team'] for other in expected.values() if
                           (scenario['format']=='2v2' and other['focal']==p['focal']) or other==p)
                if receipt['allies']!=allies: raise IntegrityError('alliance mismatch')
                disabled=settings.get(str(p['player']),{}).get(NO_ORDERS,False)
                if receipt.get('orders_disabled',False)!=disabled: raise IntegrityError('no-orders treatment mismatch')
                if disabled and not p['focal']: raise IntegrityError('nonfocal no-orders contamination')
                if ai==7:
                    for key in ('requested','actual'):
                        values={r['key']:r['value'] for r in receipt[key]['parameters']}
                        if values!={k:v for k,v in settings[str(p['player'])].items() if k!=NO_ORDERS}: raise IntegrityError(f'{key} settings mismatch')
        for r in records:
            if r['type'] in ('order_issued','order_dispatched','decision'):
                if r['player'] not in expected or r['team']!=expected[r['player']]['team']: raise IntegrityError('event player/team mismatch')
                if settings.get(str(r['player']),{}).get(NO_ORDERS,False): raise IntegrityError('disabled AI issued an order or decision')
        end=ends[0]; focal=[p for p in end['players'] if expected[p['player']]['focal']]
        outcome=1 if any(p['won'] for p in focal) else 0 if all(p['lost'] for p in focal) or end['game_ended'] else None
        return {'start':starts[0],'terminal':end,'outcome':outcome,
                'orders_issued':sum(r['type']=='order_issued' for r in records),
                'orders_dispatched':sum(r['type']=='order_dispatched' for r in records),
                'decision_events':sum(r['type']=='decision' for r in records),'audit_sha256':sha(path)}
    except (ValueError,KeyError,IndexError,TypeError,OSError) as error:
        raise IntegrityError(f'missing/malformed audit: {error}') from error


def adjudicate(protocol, scenario, receipt):
    """Keep formal outcomes intact; adjudicate only complete cutoff receipts.

    The binary estimand is sole tournament-win probability. A draw is recorded
    explicitly and scores zero for every side, rather than becoming missing data.
    """
    natural=receipt['outcome']
    end=receipt['terminal']
    result={'natural_outcome':natural, 'outcome':natural,
            'decision_reason':'natural_win' if natural==1 else 'natural_loss' if natural==0 else 'unresolved'}
    if natural is not None or protocol.get('cutoff_scoring')!='population_v1':
        return result
    if end['tick']!=protocol['hard_tick_limit'] or end['game_ended']:
        raise IntegrityError('population adjudication requires an exact unfinished cutoff')
    expected={p['player']:p for p in scenario['players']}
    if Counter(p['player'] for p in end['players'])!=Counter({p:1 for p in expected}):
        raise IntegrityError('population player receipt mismatch')
    sides={}; focal_side=None; seen=set()
    for p in end['players']:
        spec=expected[p['player']]
        if p['team']!=spec['team'] or p['team'] in seen:
            raise IntegrityError('population team mapping mismatch or duplicate team')
        seen.add(p['team'])
        population=p.get('population')
        if type(population) is not int or population<0:
            raise IntegrityError('missing or invalid exact population')
        side=('focal' if spec['focal'] else 'opponents') if scenario['format']=='2v2' else str(p['team'])
        if spec['focal']: focal_side=side
        value=sides.setdefault(side, {'population':0,'eligible':False})
        if p['alive'] and not p['lost']:
            value['population']+=population
            value['eligible']=True
    eligible={side:value['population'] for side,value in sides.items() if value['eligible']}
    if not eligible or focal_side is None:
        raise IntegrityError('unfinished cutoff has no eligible side or focal mapping')
    highest=max(eligible.values())
    leaders=sorted(side for side,population in eligible.items() if population==highest)
    draw=len(leaders)>1
    result.update(outcome=int(not draw and focal_side==leaders[0]),
                  decision_reason='population_draw' if draw else 'population_win' if focal_side==leaders[0] else 'population_loss',
                  population_by_side={side:v['population'] for side,v in sides.items()},
                  cutoff_leaders=leaders, cutoff_draw=draw)
    return result


def deterministic_signature(receipt):
    return {k:receipt[k] for k in ('tick','world_checksum','rng','players')}


def verify_freeze(protocol):
    raw={k:v for k,v in protocol.items() if k!='protocol_id'}
    if identity(raw)!=protocol['protocol_id']: raise IntegrityError('protocol changed')
    if sha(protocol['binary'])!=protocol['binary_sha256']: raise IntegrityError('binary changed')
    for name,digest in protocol['runtime_and_analysis'].items():
        if sha(ROOT/name)!=digest: raise IntegrityError(f'frozen file changed: {name}')
    for maps in protocol['maps'].values():
        for m in maps:
            if sha(ROOT/m['path'])!=m['sha256']: raise IntegrityError('map changed')


def require_gates(out,protocol,stage):
    if (out/'STOP_DISPATCH.json').exists() or (out/'RETIRED.json').exists(): raise IntegrityError('dispatch stopped')
    gates=json.loads((out/'qualification.json').read_text())
    if gates.get('protocol_id')!=protocol['protocol_id']: raise IntegrityError('qualification belongs to another freeze')
    needed=REQUIRED_GATES if stage in ('pilot','confirmation','combination') else (
        'routing','opponent_identity','player_isolation','save_load','behavioral',
        'reproducibility_across_hosts','statistical_sensitivity')
    if protocol.get('controls',{}).get('kind')=='no_orders': needed=(*needed,'no_orders')
    if protocol.get('cutoff_scoring')=='population_v1': needed=(*needed,'population_scoring')
    for gate in needed:
        evidence=gates.get(gate,{})
        if evidence.get('status')!='passed' or not evidence.get('path') or sha(out/evidence['path'])!=evidence.get('sha256'):
            raise IntegrityError('unqualified gate: '+gate)
    if stage=='confirmation':
        allocation=json.loads((out/'allocation.json').read_text())
        if allocation.get('protocol_id')!=protocol['protocol_id'] or not allocation.get('approved'): raise IntegrityError('compute allocation required')
    if stage=='combination' and not (out/'confirmed-switches.json').exists(): raise IntegrityError('confirmed harmful switches required')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare',action='store_true'); parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not args.prepare: parser.error('select --prepare')
    print(prepare(args.binary,args.output)['protocol_id'])
