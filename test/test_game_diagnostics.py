#!/usr/bin/env python3
"""Production diagnostics CLI contracts and off/on continuation evidence."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def main(binary=None):
    from test_map_cli import png
    if binary is None:
        from build_paths import native_binary
        binary = Path(sys.argv[1] if len(sys.argv) > 1 else native_binary()).resolve()
    output = ROOT / 'artifacts/map-cli/diagnostics'
    output.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(prefix='glob2-diagnostics-') as profile:
        env = dict(os.environ, GLOB2_USER_DIR=profile, SDL_VIDEODRIVER='invalid-for-export', SDL_AUDIODRIVER='dummy')
        env.pop('GLOB2_SIM_THREAD', None)
        def run(name, arguments, extra=None, ok=True):
            command = [str(binary), *map(str, arguments)]
            started = time.monotonic()
            result = subprocess.run(command, cwd=ROOT, env=dict(env, **(extra or {})), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
            (output / (name+'.log')).write_bytes(result.stdout)
            records.append({'name': name, 'command': command, 'environment': extra or {}, 'exit_code': result.returncode, 'elapsed_seconds': time.monotonic()-started})
            require((result.returncode == 0) == ok, f'{name}: exit {result.returncode}\n{result.stdout.decode(errors="replace")[-4000:]}')
            return result
        def game(name, options=(), extra=None, ticks=1200):
            target = output/name
            # Fresh runs get new output directories. Retained evidence stays intact.
            suffix = 0
            while target.exists():
                suffix += 1
                target = output/(name+f'-{suffix}')
            run(target.name, ['--run-game', '--load-game', initial, '--ticks',ticks,'--telemetry','checksums','--save','final','--output-dir',target,*options],extra)
            require(json.loads((target/'result.json').read_text())['status']=='completed', name+' did not complete')
            return target
        initial_run = output / ('initial-'+str(time.time_ns()))
        run('initial', ['--run-game','--generator','15','--map-seed','42','--param','width=6','--param','height=6','--param','teams=2','--game-seed','731','--player','maxima','--player','maxima','--ticks','1','--save','initial','--output-dir',initial_run])
        initial = initial_run/'initial.game.gz'
        require(initial.exists(),'missing initial save')
        original = hashlib.sha256(initial.read_bytes()).hexdigest()
        baseline = game('off')
        options = ['--diagnostic-fields','maxima','--diagnostic-interval','500']
        fields = game('fields',options)
        painted = game('png',options+['--diagnostic-png','true'])
        threaded = game('threaded',options+['--diagnostic-png','true'],{'GLOB2_SIM_THREAD':'1'})
        for target in (fields, painted, threaded):
            require((target/'game.replay.checksums').read_bytes() == (baseline/'game.replay.checksums').read_bytes(), 'diagnostics changed per-tick simulation: '+str(target))
            require(gzip.decompress((target/'final.game.gz').read_bytes()) == gzip.decompress((baseline/'final.game.gz').read_bytes()), 'diagnostics changed final save: '+str(target))
            summary = json.loads((target/'diagnostics/summary.json').read_text())
            require(summary['completed'] >= 2 and summary['failed'] == summary['skipped'] == 0, 'incomplete capture: '+str(summary))
            for capture in (target/'diagnostics').glob('tick-*'):
                metadata = json.loads((capture/'capture.json').read_text())
                require(metadata['complete'] and metadata['schema_version']==1,'invalid completion metadata')
                require(len(list(capture.glob('*.field')))==5,'missing fields')
                if target != fields:
                    images = list(capture.glob('*.png'))
                    require(len(images)==5,'missing automatic PNGs')
                    for path in images:
                        header=path.read_bytes()[:24]
                        require(header[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',header[16:24])==(2048,2048),'wrong automatic PNG dimensions')
            if target != fields:
                sample=next((target/'diagnostics').glob('tick-*/*.png'))
                require(len(set(png(sample)[2]))>8,'blank automatic PNG')
        # Continue both final saves and compare every subsequent tick.
        initial = baseline/'final.game.gz'
        continued = game('continued-off',ticks=1500)
        initial = painted/'final.game.gz'
        continued_png = game('continued-png', options, ticks=1500)
        require((continued/'game.replay.checksums').read_bytes()==(continued_png/'game.replay.checksums').read_bytes(),'diagnostic save continuation diverged')
        initial = initial_run/'initial.game.gz'
        rendered=output/'standalone.png'
        run('render',['--render-game',initial,'--output',rendered,'--render-max-pixels','128'])
        width,height,pixels=png(rendered)
        require((width,height)==(128,128) and len(set(pixels))>8,'blank or wrongly sized render')
        large=output/'large.map.gz'
        run('large-map',['--generate-map','maze','--seed','7','--width','512','--height','512','--teams','2','--output',large])
        run('large-render',['--render-game',large,'--output',output/'large.png','--render-max-pixels','128'])
        require(png(output/'large.png')[:2]==(128,128),'large map ignored output cap')
        captured_field = next((fields/'diagnostics').glob('tick-*/threat.field'))
        run('overlay',['--render-game',initial,'--output',output/'overlay.png','--render-max-pixels','128','--render-field',captured_field,'--field-color','255,40,40'])
        for i, extra in enumerate((['--render-max-pixels','0'],['--render-max-pixels','8193'],['--field-color','1,2,3'],['--render-field',captured_field,'--field-color','256,0,0'],['--preview-size','128'])):
            run('invalid-render-'+str(i),['--render-game',initial,'--output',rendered,*extra],ok=False)
        for i, extra in enumerate((['--diagnostic-interval','1'],['--diagnostic-fields','unknown'],['--diagnostic-fields','maxima','--diagnostic-interval','0'],['--diagnostic-fields','maxima','--diagnostic-png','maybe'])):
            run('invalid-run-'+str(i),['--run-game','--load-game',initial,'--ticks','1','--output-dir',output/('invalid-'+str(i)),*extra],ok=False)
        # A file where the diagnostics directory should be must not abort the game.
        failure=output/('write-failure-'+str(time.time_ns()))
        failure.mkdir(); (failure/'diagnostics').write_text('blocked')
        run('write-failure',['--run-game','--load-game',initial,'--ticks','1200','--output-dir',failure,'--telemetry','checksums',*options])
        require((failure/'game.replay.checksums').read_bytes()==(baseline/'game.replay.checksums').read_bytes(),'output failure changed simulation')
        require(not list(output.rglob('*.tmp')),'partial capture directories leaked')
        require(hashlib.sha256(initial.read_bytes()).hexdigest()==original,'input save changed')
    (output/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
    print(f'PASS {len(records)} diagnostics commands; evidence: {output}')

if __name__ == '__main__':
    sys.path.insert(0,str(ROOT/'tools'))
    main()
