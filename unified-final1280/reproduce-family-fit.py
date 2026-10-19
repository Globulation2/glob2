"""Reproduce the selected combined fallback and four family experts.

Run from the tested source checkout with the recorded pinned training environment.
This intentionally performs a fit; packaging or syntax-checking this helper does not.
It preserves archived cohort metadata, censored complete rows and grouped holdouts.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

root = Path(__file__).resolve().parent
checkout = Path.cwd()
if not (checkout / 'tools/adaptive_gradient_model.py').is_file():
    raise SystemExit('Run from the tested checkout root')
sys.path.insert(0, str(checkout))
from tools import adaptive_gradient_dataset as dataset
from tools import adaptive_gradient_model as model
from tools.resource_gradient_model import HEADS


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


for archive in json.loads((root / 'TRAINING-ARCHIVES.json').read_text())['archives']:
    source = root / archive['archive']
    if sha256(source) != archive['sha256']:
        raise ValueError('Archive hash mismatch: ' + source.name)
    with tarfile.open(source) as bundle:
        for member in bundle:
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError('Only regular archived provenance/data files are allowed')
            target = (root / member.name).resolve()
            if not target.is_relative_to(root.resolve()):
                raise ValueError('Archive path escapes evidence root')
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                continue
            with bundle.extractfile(member) as incoming, target.open('wb') as outgoing:
                shutil.copyfileobj(incoming, outgoing)
    for entry in archive['files']:
        path = root / entry['path']
        if path.stat().st_size != entry['bytes'] or sha256(path) != entry['sha256']:
            raise ValueError('Archived input identity mismatch: ' + entry['path'])

if tuple(dataset.SELECTED_FEATURES) != tuple(dataset.FEATURES) or len(dataset.FEATURES) != 12:
    raise ValueError('Reproduction requires the selected twelve-input contract')
expected = json.loads((root / 'family-models/selected/deployment.json').read_text())
model.validate(expected)
inputs = json.loads((root / 'terminal-inputs.json').read_text())
registry = root / 'map-splits.json'
registry_hash = sha256(registry)
output = root / 'reproduced-family-fit.json'
command = [sys.executable, 'tools/adaptive_gradient_model.py', 'fit',
           '--split-registry', str(registry), '--output', str(output),
           '--collector-contract', 'unified-gradient-spatial-h128-v3',
           '--expected-families', 'materials,guard,clear,forbidden',
           '--threshold-milli', '16000']
for manifest in inputs['manifests']:
    command.extend(['--manifest', str(root / manifest)])
subprocess.run(command, check=True)
if sha256(registry) != registry_hash:
    raise ValueError('Fit modified the persistent map split registry')
actual = json.loads(output.read_text())
for key in ('features', 'transforms', *HEADS):
    if actual[key] != expected[key]:
        raise ValueError('Combined fallback mismatch: ' + key)
if set(actual['family_experts']) != set(expected['family_experts']):
    raise ValueError('Measured expert support changed')
for identifier, expert in expected['family_experts'].items():
    for key in ('family', 'training_support', *HEADS):
        if actual['family_experts'][identifier][key] != expert[key]:
            raise ValueError('Expert mismatch: ' + identifier + '/' + key)
print('Exact shared transforms, combined fallback and all four expert integer heads match.')
