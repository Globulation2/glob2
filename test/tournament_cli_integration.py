#!/usr/bin/env python3
"""Real main-binary CLI/save integration; retains exact inputs, logs and tick traces."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_paths import native_binary
import argparse
import hashlib
import platform
import json
import os
import struct
import subprocess
import tempfile


def tick_records(path):
    data=Path(path).read_bytes()
    if data[:4]!=b'GCS1': raise ValueError('not a checksum trace')
    teams,players,count,flags=struct.unpack_from('<4I',data,4)
    position=20;rows={}
    for _ in range(count):
        start=position;tick,checksum=struct.unpack_from('<2I',data,position);position+=8
        for team in range(teams):
            position+=4
            for kind in range(2):
                n=struct.unpack_from('<I',data,position)[0];position+=4
                for item in range(n):
                    gid,cs,size=struct.unpack_from('<HII',data,position);position+=10+size*4
        rows[tick]=data[start:position]
    if position!=len(data): raise ValueError('truncated/extra trace bytes')
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',default=str(native_binary()));parser.add_argument('--output')
    parser.add_argument('--initial',help='retained .game input for cross-platform trace comparison')
    parser.add_argument('--ticks',type=int,default=2048)
    args=parser.parse_args()
    binary=Path(args.binary).resolve();root=Path(__file__).resolve().parents[1]
    output=Path(args.output).resolve() if args.output else Path(tempfile.mkdtemp(prefix='glob2-cli-tests-'))
    output.mkdir(parents=True,exist_ok=True)
    records=[]
    def run(name,command,expected=0,environment=None):
        target=output/name
        if target.exists(): raise ValueError('test output already exists: '+str(target))
        env=dict(os.environ);env['HOME']=str(output/'home');Path(env['HOME']).mkdir(exist_ok=True)
        if environment:env.update(environment)
        full=[str(binary),*command,'--output-dir',str(target)]
        with (output/(name+'.log')).open('w') as log:
            result=subprocess.run(full,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        assert result.returncode==expected,(name,result.returncode,(output/(name+'.log')).read_text()[-4000:])
        value=json.loads((target/'result.json').read_text())
        records.append({'name':name,'command':full,'exit_code':result.returncode,'status':value['status']})
        return value,target
    if args.initial:
        _,target=run('cross-platform',['--run-game','--load-game',str(Path(args.initial).resolve()),'--ticks',str(args.ticks),'--telemetry','checksums','--save','final'])
        print(target/'game.replay.checksums');return
    # Linux FileManager used to assert for executable paths longer than 99 bytes.
    import shutil
    long_dir=output/('bundle-'+'a'*80)/('package-'+'b'*80)
    long_dir.mkdir(parents=True)
    long_binary=long_dir/binary.name
    try:os.link(binary,long_binary)
    except OSError:shutil.copy2(binary,long_binary)
    catalog=json.loads(subprocess.check_output([str(long_binary),'--headless-catalog'],cwd=root))
    long_binary.unlink()

    assert {a['name'] for a in catalog['ais']}=={'numbi','castor','warrush','econo','nicowar','cortex','maxima','cabino'}
    (output/'catalog.json').write_text(json.dumps(catalog,indent=2))
    generated,map_dir=run('map',['--generate-map','--generator','15','--map-seed','42','--param','teams=2','--write-map','true','--rotations','2'])
    assert catalog['map_report_version']==2 and catalog['generation_telemetry_version']==1
    report=generated['map_report']
    assert report['schema_version']==2 and report['report_type']=='map'
    assert report['generation']['telemetry']['enabled']
    assert report['generation']['telemetry']['records']
    native_report=output/'native-map-report.json'
    subprocess.run([str(binary),'--generate-map','symmetric-arena','--seed','42','--teams','2',
                    '--json',str(native_report)],cwd=root,check=True,stdout=subprocess.DEVNULL,
                   env=dict(os.environ,GLOB2_USER_DIR=str(output/'native-profile')))
    assert json.loads(native_report.read_text())==report, 'native and distributed report contracts diverged'
    assert generated['rotations_verified'] and generated['request']['teams']==2
    assert generated['quality']['colonies'] and 'worst_wheat_distance' in generated['statistics']
    # Loading generated towers populates resource call lists, moving the map
    # offset. Canonicalization must prime that offset without weakening either
    # the reload-idempotence or full team-rotation byte equality checks.
    for generator,seed in [(24,4143377922),(23,2306931438),(22,3110978615)]:
        rotated,directory=run(f'rotation-offset-{generator}',[
            '--generate-map','--generator',str(generator),'--map-seed',str(seed),
            '--param','width=7','--param','height=7','--param','teams=2',
            '--param','workers=4','--candidates','0','--rotations','2','--write-map','true'])
        assert rotated['rotations_verified'],(generator,seed)
        assert all((directory/f'map-r{rotation}.map').is_file() for rotation in range(2))
    invalid,_=run('invalid-generator',['--generate-map','--generator','15','--map-seed','42','--param','teams=0'],2)
    assert invalid['status']=='invalid_request'
    assert invalid['map_report']['report_type']=='generation_failure'
    assert invalid['map_report']['generation']['telemetry']['records']
    base=['--run-game','--map-file',str(map_dir/'map-r0.map'),'--player','cortex','--player','cortex',
          '--game-seed','19','--ticks',str(args.ticks),'--telemetry','checksums']
    params=['--ai-param','0:swarmWorkerCap=4','--ai-param','1:swarmWorkerCap=7',
            '--ai-param','0:expandDebounceCycles=3','--ai-param','1:expandDebounceCycles=5']
    value,original=run('configured',base+params+['--save','initial','--save','final','--save','every:512'])
    assert value['termination']=='tick_cap' and value['unresolved'] and value['winning_teams']==[]
    assert 'swarmWorkerCap=4' in value['players'][0]['runtime_values']
    assert 'swarmWorkerCap=7' in value['players'][1]['runtime_values']
    assert all('standard_statistics' in t and 'history' in t for t in value['teams'])
    original_ticks=tick_records(original/'game.replay.checksums')
    for name,save in [('initial-reload','initial.game'),('continuation','checkpoint-512.game')]:
        restored,target=run(name,['--run-game','--load-game',str(original/save),'--ticks',str(args.ticks),'--telemetry','checksums','--save','final'],
                            environment={'GLOB2_CORTEX_TUNING':'/nonexistent/ambient','GLOB2_MAXIMA_OVERRIDES':'invalid=1','GLOB2_CORTEX_POLICY':'ml'})
        trace=tick_records(target/'game.replay.checksums')
        assert trace and all(original_ticks[t]==record for t,record in trace.items()),name
        assert restored['players']==value['players']
    _,normal=run('defaults',base)
    assert tick_records(normal/'game.replay.checksums')!=original_ticks,'overrides must affect actual execution'
    _,isolated=run('isolated',base,environment={'GLOB2_CORTEX_TUNING':'/nonexistent/ambient','GLOB2_CORTEX_POLICY':'ml','GLOB2_CHECKSUM_SIDECAR_MAX_TICKS':'1'})
    assert tick_records(normal/'game.replay.checksums')==tick_records(isolated/'game.replay.checksums')
    run('invalid-override',base+['--ai-param','0:tierMidDiv=0'],2)
    run('duplicate-override',base+['--ai-param','0:swarmWorkerCap=4','--ai-param','0:swarmWorkerCap=5'],2)
    for name in ('maxima',):
        config=['--run-game','--map-file',str(map_dir/'map-r0.map'),'--player','maxima','--player','maxima',
                '--game-seed','23','--ticks',str(args.ticks),'--telemetry','checksums',
                '--ai-param','0:staffing.new_inn_workers=3','--ai-param','1:staffing.new_inn_workers=5','--save','every:512']
        maximum,directory=run(name,config)
        restored,target=run('maxima-continuation',['--run-game','--load-game',str(directory/'checkpoint-512.game'),
                            '--ticks',str(args.ticks),'--telemetry','checksums'])
        first=tick_records(directory/'game.replay.checksums');second=tick_records(target/'game.replay.checksums')
        assert all(first[t]==record for t,record in second.items()),'Maxima configured continuation'
        assert maximum['players']==restored['players']
    allied,target=run('allied-winners',['--run-game','--generator','15','--map-seed','76','--param','teams=4',
                      '--game-seed','5','--player','econo','--player','econo','--player','econo','--player','econo',
                      '--alliance','1','--alliance','1','--alliance','1','--alliance','1','--ticks','256','--save','initial'])
    assert allied['winning_teams']==[0,1,2,3] and allied['winning_alliances']==[1],allied
    assert 'generation' in allied
    legacy=root/'test/fixtures/team-stats/version88.game'
    run('empty-player-save',['--run-game','--load-game',str(legacy),'--ticks','10000'],2)
    run('legacy-v84',['--run-game','--load-game',str(root/'games/gd-small-2ai.game'),'--ticks','10000'])
    (output/'verification.json').write_text(json.dumps({'passed':True,'cases':records,'ticks':args.ticks,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'platform':platform.platform()},indent=2))
    print(f'PASS {len(records)} main-binary cases; retained artifacts: {output}')


if __name__=='__main__':main()
