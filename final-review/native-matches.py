import hashlib,json,os,subprocess
from pathlib import Path
root=Path.cwd(); out=root/'artifacts/final-pr-review/matches'; out.mkdir(parents=True,exist_ok=True)
binary=root/'build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
results=[]
for name,teams in [('Holiday_Island_2',4),('Oazis',11)]:
    commands=[]
    for participants in (1,4):
        dest=out/f'{name}-p{participants}'
        cmd=[str(binary),'game','run','--map-file',str(root/f'maps/{name}.map.gz'),'--game-seed','42','--ticks','2048','--compute-threads',str(participants),'--gpu','off','--gradient-backend','cpu','--telemetry','checksums','--output-dir',str(dest)]
        for t in range(teams): cmd+=['--player',('nicowar','econo')[t%2]]
        commands.append(cmd)
        with (out/f'{name}-p{participants}.log').open('w') as log:
            subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    pair={'map':name,'map_sha256':hashlib.sha256((root/f'maps/{name}.map.gz').read_bytes()).hexdigest(),'seed':42,'commands':commands,'traces':{}}
    for file in ('world.checksums','game.replay.checksums'):
        a=(out/f'{name}-p1'/file).read_bytes(); b=(out/f'{name}-p4'/file).read_bytes()
        assert a==b,(name,file)
        pair['traces'][file]={'sha256':hashlib.sha256(a).hexdigest(),'lines':len(a.splitlines()),'matched':True}
    results.append(pair)
    print(name,'all per-tick checksums matched',flush=True)
for kind in ('pending','queued'):
    commands=[]
    for participants in (1,4):
        dest=out/f'format172-{kind}-p{participants}'
        cmd=[str(binary),'game','run','--load-game',str(root/f'artifacts/lazy-restore/format172-{kind}.game'),'--ticks','16','--compute-threads',str(participants),'--telemetry','checksums','--output-dir',str(dest)]
        commands.append(cmd)
        with (out/f'format172-{kind}-p{participants}.log').open('w') as log:
            subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    pair={'save':f'format172-{kind}.game','commands':commands,'traces':{}}
    for file in ('world.checksums','game.replay.checksums'):
        a=(out/f'format172-{kind}-p1'/file).read_bytes(); b=(out/f'format172-{kind}-p4'/file).read_bytes()
        assert a==b,(kind,file)
        pair['traces'][file]={'sha256':hashlib.sha256(a).hexdigest(),'lines':len(a.splitlines()),'matched':True}
    results.append(pair)
    print(kind,'old-save per-tick checksums matched',flush=True)
(out/'comparison.json').write_text(json.dumps(results,indent=2)+'\n')

# Cleanup is behavior-preserving: also compare with the recorded pre-cleanup revision.
for case in results:
    label=case.get('map') or 'format172-'+case['save'].removeprefix('format172-').removesuffix('.game')
    for participants in (1,4):
        for file in ('world.checksums','game.replay.checksums'):
            name=f'{label}-p{participants}'
            assert (out/name/file).read_bytes()==(root/'artifacts/flyer-default/matches'/name/file).read_bytes(),(name,file)
print('Cleanup matches pre-cleanup revision on every world/entity tick',flush=True)
