#!/usr/bin/env python3
"""Exercise map CLI modes in a disposable profile; PNG checks require OpenGL."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
GENERATION_ONLY = '--generation-only' in sys.argv
arguments = [arg for arg in sys.argv[1:] if arg != '--generation-only']
BINARY = Path(arguments[0] if arguments else ROOT / 'build/src/glob2').resolve()
OUT = ROOT / 'artifacts/map-cli'
if GENERATION_ONLY:
    OUT = OUT / 'generation-only'
OUT.mkdir(parents=True, exist_ok=True)


def png(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset, compressed = 8, bytearray()
    while offset < len(data):
        size, kind = struct.unpack_from('>I4s', data, offset)
        payload = data[offset+8:offset+8+size]
        assert zlib.crc32(kind + payload) & 0xffffffff == struct.unpack_from('>I', data, offset+8+size)[0]
        if kind == b'IHDR':
            width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', payload)
            assert depth == 8 and color in (2, 6) and compression == filtering == interlace == 0
        if kind == b'IDAT':
            compressed.extend(payload)
        offset += size + 12
    bpp = 3 if color == 2 else 4
    raw = zlib.decompress(compressed)
    stride = width * bpp
    assert len(raw) == height * (stride + 1)
    previous = bytearray(stride)
    pixels = bytearray()
    for y in range(height):
        filter_type = raw[y*(stride+1)]
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for x in range(stride):
            a, b, c = row[x-bpp] if x >= bpp else 0, previous[x], previous[x-bpp] if x >= bpp else 0
            p = a+b-c
            distances = (abs(p-a), abs(p-b), abs(p-c))
            paeth = (a, b, c)[distances.index(min(distances))]
            assert 0 <= filter_type <= 4
            row[x] = (row[x] + (0, a, b, (a+b)//2, paeth)[filter_type]) & 255
        pixels.extend(v for i, v in enumerate(row) if bpp == 3 or i % 4 != 3)
        previous = row
    return width, height, bytes(pixels)


def main():
    log = []
    with tempfile.TemporaryDirectory(prefix='glob2-map-cli-') as temp:
        profile = Path(temp)
        preferences = profile / 'preferences.txt'
        preferences.write_text('rememberUnit=1\n')
        before = preferences.read_bytes(), preferences.stat().st_mtime_ns
        env = dict(os.environ, GLOB2_USER_DIR=str(profile), SDL_AUDIODRIVER='dummy')

        def run(*args, ok=True):
            args = list(map(str,args))
            if args[0] in ('--generate-map', '--preview-map') and len(args) >= 2 and '--help' not in args:
                args[2:2] = ['-d', str(ROOT)]
            result = subprocess.run([str(BINARY), *args], cwd=profile, env=env,
                                    capture_output=True, text=True, timeout=90)
            log.append({'args': list(map(str,args)), 'exit': result.returncode,
                        'stdout': result.stdout, 'stderr': result.stderr})
            (OUT/'commands.json').write_text(json.dumps(log,indent=2)+'\n')
            assert result.returncode == (0 if ok else 1), log[-1]
            return result.stdout

        assert 'coral' in run('--list-map-generators')
        catalog = run('--list-map-generators', 'maze')
        assert 'cell-shape=' in catalog and 'width=256' in catalog
        assert 'Map launch modes' in run('--generate-map', '--help')
        assert '--preview-map' in run('--help')
        config = OUT / 'maze.cfg'
        config.write_text('# Test CLI precedence even before --config\nseed=99\nwidth=256\nheight=128\nteams=4\nworkers=4\ncell-shape=1\n')
        first, second, saved, loaded = (OUT / name for name in ('cli.png','config.png','maze.map','loaded.png'))
        if GENERATION_ONLY:
            env['SDL_VIDEODRIVER'] = 'invalid'
            run('--generate-map','maze','--seed','7','--width','128','--height','128',
                '--teams','4','--set','cell-shape=0','--output',saved)
            original = saved.read_bytes()
            run('--generate-map','maze','--seed','7','--width','128','--set','cell-shape=0',
                '--config',config,'--output',saved)
            assert original == saved.read_bytes(), 'Config/CLI changed serialized map'
            run('--generate-map','maze','--set','unknown=1','--output',saved,ok=False)
            assert original == saved.read_bytes(), 'Invalid settings overwrote map'
            assert (preferences.read_bytes(), preferences.stat().st_mtime_ns) == before
            print('PASS headless map generation, serialized config/CLI equivalence, invalid settings, preferences')
            return
        run('--generate-map','maze','--seed','7','--width','128','--height','128',
            '--teams','4','--set','cell-shape=0','--preview',first,'--output',saved,'--json',OUT/'maze.json')
        assert json.loads((OUT/'maze.json').read_text())['map']['width'] == 128
        run('--generate-map','maze','--seed','7','--width','128','--set','cell-shape=0',
            '--config',config,'--preview',second)
        width, height, pixels = png(first)
        assert sum(any(pixels[i:i+3]) for i in range(0,len(pixels),3)) > width*height*0.9, 'Terrain was not drawn'
        assert png(first) == png(second), 'Config/CLI precedence changed generation'
        saved_before = saved.read_bytes()
        run('--preview-map',saved,'--output',loaded)
        assert png(first) == png(loaded), 'Generated map preview differs after loading'
        assert png(first)[:2] == (512,512)
        run('--preview-map',saved,'--output',loaded,'--preview-size','128')
        assert png(loaded)[:2] == (128,128)
        run('--preview-map',saved,'--output','relative.png')
        assert png(profile/'relative.png')[:2] == (512,512), 'CLI changed working directory'
        assert saved.read_bytes() == saved_before, 'Preview modified input map'
        for fixture in ('team-stats/version88.game','wrapped-building/reproducer.game','entering-explorer/reproducer.game'):
            source = ROOT / 'test/fixtures' / fixture
            old = source.read_bytes()
            run('--preview-map',source,'--output',OUT / (source.parent.name+'.png'))
            assert source.read_bytes() == old, 'Preview modified save'
            png(OUT / (source.parent.name+'.png'))
        rectangular = OUT / 'rectangular.png'
        run('--generate-map','maze','--seed','7','--width','256','--height','128','--preview',rectangular)
        assert png(rectangular)[:2] == (512,256), 'Preview lost map aspect ratio'
        premade = next(iter(sorted((ROOT / 'maps').glob('*.map'))))
        run('--preview-map',premade,'--output',OUT / 'premade.png')
        png(OUT / 'premade.png')
        invalid = [
            ['--generate-map'], ['--generate-map','unknown','--preview',first],
            ['--generate-map','maze'], ['--generate-map','maze','--seed'],
            ['--generate-map','maze','--seed','4294967296','--preview',first],
            ['--generate-map','maze','--seed','-1','--preview',first],
            ['--generate-map','maze','--width','127','--preview',first],
            ['--generate-map','maze','--set','unknown=1','--preview',first],
            ['--generate-map','maze','--set','cell-shape=99','--preview',first],
            ['--generate-map','maze','--set','cell-shape=1junk','--preview',first],
            ['--generate-map','maze','--set','broken','--preview',first],
            ['--generate-map','maze','--output',first,'--preview',first],
            ['--generate-map','maze','--output',saved,'--preview-size','256'],
            ['--generate-map','maze','--config',OUT/'missing.cfg','--preview',first],
            ['--preview-map',OUT/'missing.game','--output',first],
            ['--preview-map',saved,'--output',saved],
            ['--preview-map',saved,'--output',first,'--overlay','unknown'],
            ['--preview-map',saved,'--output',first,'--preview-size','0'],
            ['--preview-map',saved,'--output',first,'--preview-size','4294967295'],
            ['--preview-map',saved,'--output',first,'--seed','7'],
            ['--preview-map',saved,'--output',OUT],
        ]
        for args in invalid:
            run(*args,ok=False)
        config = OUT / 'invalid.cfg'
        config.write_text('not key=value\ninvalid line\n')
        run('--generate-map','maze','--config',config,'--preview',first,ok=False)
        assert (preferences.read_bytes(), preferences.stat().st_mtime_ns) == before
        assert not list(profile.rglob('*.replay'))
    (OUT/'commands.json').write_text(json.dumps(log,indent=2)+'\n')
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.glob('*.png')}
    (OUT/'sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
    print(f'PASS {len(log)} map CLI commands: config precedence, map round trip, preview sizes, '
          'premade map, three saves, invalid inputs and unchanged preferences.')
    print(f'Artifacts: {OUT}')


if __name__ == '__main__':
    main()
