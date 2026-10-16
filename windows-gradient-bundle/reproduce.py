#!/usr/bin/env python3
"""Reproduce receipt verification under Git checkout line-ending policies."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
BASE = '987c953698bb3f0bfd1078184773075ba3313091'
OUTPUT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('gradient_bundle', ROOT / 'tools/shaders/gradient_compute.py')
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)
files = sorted(bundle.BUNDLE_FILES | {
    'src/field/shaders/manifest.json', 'third_party/vulkan/manifest.json',
    *(f'third_party/vulkan/{name}' for name in bundle.VULKAN_FILES),
})
attributes = '''src/field/GradientComputeShaders.h text eol=lf
tools/shaders/gradient_compute.py text eol=lf
src/field/shaders/*.comp text eol=lf
src/field/shaders/*.wgsl text eol=lf
third_party/vulkan/*.h text eol=lf
third_party/vulkan/LICENSE.txt text eol=lf
'''
(OUTPUT / 'candidate.gitattributes').write_text(attributes)
result = {
    'base': BASE,
    'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
    'identicalToBase': {
        path: subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT) == (ROOT / path).read_bytes()
        for path in files
    },
    'checkouts': {},
}
for autocrlf, pinned in [('false', False), ('true', False), ('true', True)]:
    with tempfile.TemporaryDirectory(prefix='gradient-crlf-proof-') as temporary:
        target = Path(temporary) / 'checkout'
        target.mkdir()
        # Read a private index. The actual checkout, index and refs stay unchanged.
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / 'index'))
        subprocess.run(['git', 'read-tree', 'HEAD'], cwd=ROOT, env=env, check=True)
        command = ['git', '-c', 'core.autocrlf=' + autocrlf]
        if pinned:
            command += ['-c', 'core.attributesFile=' + str(OUTPUT / 'candidate.gitattributes')]
        command += ['checkout-index', '--prefix=' + str(target) + '/', '--'] + files
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        data = (target / 'src/field/GradientComputeShaders.h').read_bytes()
        proof = {'command': command, 'headerSha256': hashlib.sha256(data).hexdigest(), 'headerCRLF': data.count(b'\r\n')}
        try:
            bundle.verify(target)
            proof['verify'] = 'PASS'
        except ValueError as error:
            proof['verify'] = str(error)
        if pinned:
            header = target / 'src/field/GradientComputeShaders.h'
            header.write_bytes(header.read_bytes() + b'// changed\n')
            try:
                bundle.verify(target)
                proof['tamperedHeader'] = 'UNEXPECTED PASS'
            except ValueError as error:
                proof['tamperedHeader'] = str(error)
        result['checkouts'][f'core.autocrlf={autocrlf}' + (' + candidate LF attributes' if pinned else '')] = proof
(OUTPUT / 'checkout-reproduction.json').write_text(json.dumps(result, indent=2) + '\n')
for policy, proof in result['checkouts'].items():
    print(policy + ': ' + proof['verify'])
assert all(result['identicalToBase'].values())
assert result['checkouts']['core.autocrlf=false']['verify'] == 'PASS'
assert 'Stale gradient shader bundle' in result['checkouts']['core.autocrlf=true']['verify']
assert result['checkouts']['core.autocrlf=true + candidate LF attributes']['verify'] == 'PASS'
assert 'Stale gradient shader bundle' in result['checkouts']['core.autocrlf=true + candidate LF attributes']['tamperedHeader']
