import hashlib, json, os, resource, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
BIN = {name: OUT/name/'build/linux/client/release/client-full-direct-1111/src/glob2' for name in ('before','control','after')}
ENV = dict(os.environ)
for key in list(ENV):
    if key.startswith('GLOB2_') or key.startswith('SDL_'):
        del ENV[key]
ENV.update(GLOB2_GRADIENT_BACKEND='cpu', GLOB2_GRADIENT_ALGORITHM='buckets', GLOB2_GRADIENT_VERIFY='off', SDL_AUDIODRIVER='dummy')
rows = json.loads((OUT/'runs.json').read_text()) if (OUT/'runs.json').exists() else []

def execute(label, revision, args, directory):
    directory.mkdir(parents=True, exist_ok=True)
    command = ['taskset','-c','0,2,4,6,8,10,12,14',str(BIN[revision]),*map(str,args),'--gpu','off','--data-dir',str(ROOT/'data'),'--output-dir',str(directory),'--profile','cpu-ab']
    prior = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    with (directory/'process.log').open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=ENV, stdout=log, stderr=subprocess.STDOUT)
    current = resource.getrusage(resource.RUSAGE_CHILDREN)
    row = dict(label=label, revision=revision, command=command, exit=result.returncode, wall_seconds=time.monotonic()-started, process_cpu_seconds=current.ru_utime+current.ru_stime-prior.ru_utime-prior.ru_stime, load=os.getloadavg())
    if result.returncode:
        raise RuntimeError(f'{label}: exit {result.returncode}: {directory}/process.log')
    row['result'] = json.loads((directory/'result.json').read_text())
    if row['result'].get('gpu_requested',0) or row['result'].get('gradient_gpu_fields',0):
        raise RuntimeError(f'Unexpected GPU selection/execution: {label}')
    rows.append(row)
    (OUT/'runs.json').write_text(json.dumps(rows,indent=2)+'\n')
    print(label, round(row['process_cpu_seconds'],3), 'CPU seconds', flush=True)
    return row

fixtures = []
for size in (() if (OUT/'fixtures.json').exists() else (128,256,512)):
    directory = OUT/'fixtures'/str(size)
    if size == 128:
        args = ['game','run','--map-file',ROOT/'maps/FourSquares1.map.gz','--game-seed','713','--player','castor','--player','cortex','--player','castor','--player','cortex']
    else:
        args = ['game','run','--generator','15','--map-seed','713','--set',f'width={size.bit_length()-1}','--set',f'height={size.bit_length()-1}','--set','teams=2','--game-seed','713','--player','castor','--player','cortex']
    execute(f'fixture-{size}','before',args+['--ticks','4096' if size==128 else '1','--compute-threads','1','--save','initial','--save','final'],directory)
    fixtures.append(dict(name=f'{size}-early', save=str(directory/'initial.game.gz'), start=0))
    if size == 128:
        fixtures.append(dict(name='128-established',save=str(directory/'final.game.gz'),start=4096))
if fixtures: (OUT/'fixtures.json').write_text(json.dumps(fixtures,indent=2)+'\n')
else: fixtures=json.loads((OUT/'fixtures.json').read_text())
if '--prepare' in sys.argv: sys.exit(0)
(OUT/'binary-hashes.json').write_text(json.dumps({k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in BIN.items()},indent=2)+'\n')

# Validate trajectories separately so checksum hashing does not enter timings.
for fixture in fixtures:
    reference = None
    for count in (1,2,4,8):
        for revision in BIN:
            label=f'validate-{fixture["name"]}-{count}-{revision}'
            directory=OUT/'validation'/label
            execute(label,revision,['game','run','--load-game',fixture['save'],'--ticks',fixture['start']+2304,'--compute-threads',count,'--telemetry','checksums'],directory)
            checksums=(directory/'world.checksums').read_bytes()
            if reference is None: reference=checksums
            if checksums != reference: raise RuntimeError(f'Per-tick checksum mismatch: {label}')

# Interleave fixtures and rotate ordering; run pairs are the statistical units.
for pair in range(8):
    for fixture in fixtures:
        for count in (1,2,4,8):
            revisions=list(BIN) if pair%2==0 else list(reversed(BIN))
            checksums=[]
            for revision in revisions:
                label=f'pair-{pair}-{fixture["name"]}-{count}-{revision}'
                row=execute(label,revision,['game','run','--load-game',fixture['save'],'--ticks',fixture['start']+2304,'--compute-threads',count,'--benchmark-warmup','256'],OUT/'timed'/label)
                row.update(pair=pair,fixture=fixture['name'],count=count)
                if row['result']['benchmark_measured_ticks'] != 2048: raise RuntimeError(f'Wrong measurement window: {label}')
                checksums.append(row['result']['finalChecksum'])
            if len(set(checksums)) != 1: raise RuntimeError(f'Final checksum mismatch: {label}')
    (OUT/'runs.json').write_text(json.dumps(rows,indent=2)+'\n')
print('COMPLETE',flush=True)
