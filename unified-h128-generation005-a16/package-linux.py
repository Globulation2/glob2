import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path('/home/bradley/glob2-gradient-unified-h128')
base = Path(__file__).resolve().parent.parent
runtime = base / 'runtime-linux-frozen'
runtime.mkdir(exist_ok=True)
sys.path.insert(0, str(root))
from tools.tournaments.bundles import register_bundle
from tools.tournaments.common import atomic_json, file_hash
from tools.resource_gradient_dataset import canonical_hash
from tools.adaptive_gradient_spatial import CONTRACT

revision = '4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e'
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip() == revision
assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()
assert not subprocess.check_output(['git', 'diff', '--name-only', '9340d9885', revision, '--', 'data', 'maps'], cwd=root, text=True).strip()
old = Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/fleet-linux-934/runtime')
for part in ['data', 'maps', 'lib']:
    shutil.copytree(old / part, runtime / part, dirs_exist_ok=True)
(runtime / 'bin').mkdir(exist_ok=True)
(runtime / 'model').mkdir(exist_ok=True)
shutil.copy2(base / 'linux/frozen/glob2', runtime / 'bin/glob2')
shutil.copy2(base / 'linux/frozen/adaptive-gradient.json', runtime / 'model/adaptive-gradient.json')
shutil.copy2(base / 'linux/frozen/AdaptiveGradientModel.h', runtime / 'model/AdaptiveGradientModel.h')
assert file_hash(runtime / 'bin/glob2') == json.loads((base / 'linux/FROZEN.json').read_text())['files']['glob2']['sha256']
assert json.loads((runtime / 'model/adaptive-gradient.json').read_text())['trained'] is True
assets = json.loads((old / 'asset-sha256.json').read_text())
assert all(file_hash(runtime / p) == h for p, h in assets.items())
assert all(file_hash(runtime / p.relative_to(old)) == file_hash(p) for p in (old / 'lib').rglob('*') if p.is_file())
atomic_json(runtime / 'asset-sha256.json', assets)
shutil.copy2(old / 'linked-libraries.json', runtime / 'linked-libraries.json')
paths = subprocess.check_output(['git', 'ls-files', 'src', 'libgag', 'libusl', 'SConstruct', 'scons', 'test'], cwd=root, text=True).splitlines()
atomic_json(runtime / 'native-source-sha256.json', [{'path': p, 'sha256': file_hash(root / p)} for p in paths if (root / p).is_file()])
provenance = dict(source_revision=revision,actual_native_build_revision=revision, binary_sha256=file_hash(runtime / 'bin/glob2'),
                  compiler='GCC13.3.0', libc='glibc2.39', ordinary_release=True,
                  model_sha256=file_hash(runtime / 'model/adaptive-gradient.json'),
                  header_sha256=file_hash(runtime / 'model/AdaptiveGradientModel.h'),
                  model_trained=True, refresh_threshold_milli=16000, sim_revision=90, save_format=180,
                  collector_contract=CONTRACT['id'], research_candidate_only=True,
                  native_source_manifest_sha256=file_hash(runtime / 'native-source-sha256.json'),
                  private_dependencies='exact immutable934private runtime libraries, unchanged pinned build prefixes',
                  created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
atomic_json(runtime / 'runtime-provenance.json', provenance)
for name in ['PARITY.json', 'FROZEN.json']:
    shutil.copy2(base / 'linux' / name, runtime / name)
os.environ['LD_LIBRARY_PATH'] = str(runtime / 'lib/glob2')
capabilities = json.loads(subprocess.check_output([str(runtime / 'bin/glob2'), 'info', 'catalog', '--format', 'json'], cwd=runtime, timeout=60))
atomic_json(runtime / 'capabilities.json', capabilities)
model=json.loads((runtime / 'model/adaptive-gradient.json').read_text())
assert model['threshold']==16000 and 'ThresholdMilli=16000' in (runtime / 'model/AdaptiveGradientModel.h').read_text()
legacy_collector=dict(CONTRACT,refresh_threshold_milli=128000)
assert canonical_hash(legacy_collector)=='40c320132ae1a3332eab59a7742b6fb9ee02d03902a28ba6773ad7eb549f8fc2'
options = dict(release=1, optimization='O3', dev_fast=0, pch=0, unity=0,
               model_sha256=provenance['model_sha256'], header_sha256=provenance['header_sha256'],
               adaptive_gradient_contract_sha256=canonical_hash(legacy_collector),
               adaptive_gradient_label_contract_sha256=canonical_hash(CONTRACT),
               collector_declaration=legacy_collector,
               legacy_collector_declaration_note='Historical threshold field is not active deployment behavior; actual threshold is attested separately',
               refresh_threshold_milli=model['threshold'],unified_model_trained=True,
               model_contract='unified-gradient-h128-v2', collector_contract=CONTRACT['id'],
               runtime_provenance=provenance, source_revision=revision, stable_incumbent_promotion=False)
bundles = {}
for pool in ['small-pharaoh', 'middle-linux']:
    bundles[pool] = register_bundle(runtime, base / 'bundles', 'bin/glob2', revision,
                                    options=options | {'execution_pool': pool},
                                    target_platform={'os': 'linux', 'arch': 'x86_64'}, capabilities=capabilities)
atomic_json(base / 'linux-bundles.json', bundles)
print(json.dumps({pool: b['id'] for pool, b in bundles.items()}, indent=2))
