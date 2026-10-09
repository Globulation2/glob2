import concurrent.futures
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess

root = Path.cwd()
folder = root / 'artifacts/renderer-seams'
commands = json.loads((folder / 'compile_commands-input.json').read_text())
link = shlex.split(Path('/tmp/glob2-seams-final-link').read_text())
objects = {arg for arg in link if arg.endswith('.o')}
selected = [c for c in commands if c['output'] in objects or
            (c['output'].startswith('build/darwin/client/release/libgag/') or
             c['output'].startswith('build/darwin/client/release/libusl/'))]
spec = importlib.util.spec_from_file_location('provenance', root / 'test/build_provenance.py')
provenance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provenance)
metadata = provenance.source_identity(root)
metadata.update(compiler='g++', build='fresh focused release renderer tests', flags='-g -std=gnu++20 -Wall -fPIC -O3; repository compile command flags recorded in build-receipt.json')
header = root / 'build/darwin/client/release/include/glob2/TestBuildProvenance.h'
header.write_text('#pragma once\n#define GLOB2_TEST_PROVENANCE_JSON ' + json.dumps(json.dumps(metadata, sort_keys=True)) + '\n')

def compile_one(c):
    args = [arg.replace('\\"', '"') for arg in shlex.split(c['command'])]
    output = folder / 'committed' / Path(c['output']).relative_to('build/darwin/client/release')
    output.parent.mkdir(parents=True, exist_ok=True)
    args[args.index('-o') + 1] = str(output)
    with output.with_suffix('.log').open('w') as log:
        result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Compile failed: {c['file']}; {output.with_suffix('.log')}")
    print('Compiled ' + c['file'], flush=True)
    return {'source': c['file'], 'source_sha256': hashlib.sha256(Path(c['file']).read_bytes()).hexdigest(),
            'original_output': c['output'], 'output': str(output), 'command': args}

with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    receipts = list(pool.map(compile_one, selected))
mapping = {r['original_output']: r['output'] for r in receipts}
for library in ['libgag', 'libusl']:
    archive = folder / ('lib' + library.removeprefix('lib') + '-tested.a')
    archive.unlink(missing_ok=True)
    members = [r['output'] for r in receipts if r['original_output'].startswith('build/darwin/client/release/' + library + '/')]
    subprocess.run(['ar', '-rcs', str(archive), *members], check=True)
    mapping['build/darwin/client/release/' + library + '/src/' + library + '.a'] = str(archive)
link = [mapping.get(arg, arg) for arg in link]
# The affected production objects are already in the fresh libgag archive.
link = [arg for arg in link if not (arg.endswith('.o') and '/committed/libgag/' in arg)]
link[link.index('-o') + 1] = str(folder / 'fog-seams-committed')
subprocess.run(link, check=True)
(folder / 'build-receipt.json').write_text(json.dumps({'producer': metadata, 'compiles': receipts, 'link': link}, indent=2) + '\n')
print('Fresh committed build linked successfully.', flush=True)
