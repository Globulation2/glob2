"""Derive an explicit AI-based Guard exclusion; preserve raw input and splits."""
from pathlib import Path
import csv
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import datetime

root = Path(__file__).resolve().parent
original = Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-005/cumulative-1280-research')
source = root / 'analysis-source'
sys.path.insert(0, str(source))
from tools import adaptive_gradient_collection as collection, adaptive_gradient_dataset as dataset
from tools.resource_gradient_dataset import canonical_hash, manifest_records
from tools.resource_gradient_model import HEADS
from tools import adaptive_gradient_model as models

sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
nonpainters = {'cabino', 'castor', 'cortex', 'econo', 'nicowar', 'numbi'}
inputs = json.loads((original/'terminal-inputs.json').read_text())
output_manifests = []
exclusions = []
original_hashes = {}
kept = removed = 0
removed_by_ai = {}
for manifest_name in inputs['manifests']:
    path = original / manifest_name
    original_hashes[str(path)] = sha(path)
    records, metadata = manifest_records(path)
    derived = []
    for record in records:
        old_path = Path(record['episodes'])
        if not old_path.is_absolute():
            old_path = path.parent / old_path
        assert sha(old_path) == record['episode_sha256']
        original_hashes[str(old_path)] = record['episode_sha256']
        roster = record['spec'].get('ais') or [record['spec']['ai']] * record['spec']['players']
        opener = gzip.open if old_path.suffix == '.gz' else open
        with opener(old_path, 'rb') as stream:
            lines = stream.readlines()
        header = next(csv.reader([lines[0].decode()]))
        indices = {key: header.index(key) for key in ['family','team','capture_known','movement_observed','complete','tick','key','captured_goal_count']}
        retained = [lines[0]]
        local_removed = 0
        for number, line in enumerate(lines[1:], start=2):
            values = next(csv.reader([line.decode()]))
            family = int(values[indices['family']])
            team = int(values[indices['team']])
            ai = roster[team]
            eligible = int(values[indices['complete']]) == 1 and int(values[indices['capture_known']]) and int(values[indices['movement_observed']])
            drop = family == 2 and ai in nonpainters and eligible
            if drop:
                exclusions.append(dict(manifest=manifest_name,group=record['group'],binary_sha256=record['binary_sha256'],source_episode_sha256=record['episode_sha256'],source_line=number,tick=int(values[indices['tick']]),key=int(values[indices['key']]),team=team,ai=ai,captured_goal_count=int(values[indices['captured_goal_count']]),criterion='eligible Guard episode on exact nonpainting AI seat; accepted retrospective approximation'))
                local_removed += 1
                removed_by_ai[ai] = removed_by_ai.get(ai, 0) + 1
            else:
                retained.append(line)
        target = root / 'episodes' / record['group'] / (record['binary_sha256'] + '-' + record['episode_sha256'] + '.csv.gz')
        target.parent.mkdir(parents=True,exist_ok=True)
        raw = b''.join(retained)
        with target.open('wb') as stream:
            with gzip.GzipFile(filename='', mode='wb', fileobj=stream, mtime=0) as compressed:
                compressed.write(raw)
        updated = dict(record)
        updated.update(episodes=str(target), episode_sha256=sha(target))
        updated['provenance'] = dict(record.get('provenance', {}), row_filter=dict(source_manifest_sha256=sha(path),source_episode_sha256=record['episode_sha256'],source_episodes=str(old_path),removed_rows=local_removed,retained_rows=len(retained)-1,exact_retained_line_bytes=True,criterion='eligible-guard-exact-nonpainting-ai-v1'))
        derived.append(updated)
        kept += len(retained)-1
        removed += local_removed
    destination = root / 'manifests' / manifest_name
    destination.parent.mkdir(parents=True,exist_ok=True)
    document = dict(schema=1,cohort=metadata,records=derived)
    document['content_sha256'] = canonical_hash(dict(cohort=metadata,records=derived))
    destination.write_text(json.dumps(document,indent=2)+'\n')
    output_manifests.append(destination)
assert removed == 49925, (removed, removed_by_ai)
assert all(item['captured_goal_count'] == 0 for item in exclusions)
(root/'EXCLUSIONS.json').write_text(json.dumps(dict(schema=1,criterion='eligible Guard rows on six exact nonpainting AI seats',user_accepted_approximation=True,not_a_zero_goal_filter=True,excluded=removed,by_ai=removed_by_ai,rows=exclusions),indent=2)+'\n')
shutil.copy2(original/'map-splits.json',root/'map-splits.json')
all_rows = collection.load(output_manifests)
rows = dataset.eligible(all_rows)
assert len(rows) == 287045 - 49925
assert sum(row['family']=='guard' for row in rows) == 19791
held = set(json.loads((original/'generic-research.json').read_text())['provenance']['heldout_maps'])
heldout = [row for row in rows if row['group'] in held]
summary = dict(status='DERIVED-READY-TO-FIT',source_input_sha256=sha(original/'terminal-inputs.json'),excluded_rows=removed,retained_complete_rows=len(all_rows),retained_eligible_rows=len(rows),retained_guard_rows=sum(row['family']=='guard' for row in rows),heldout_rows=len(heldout),removed_by_ai=removed_by_ai,original_split_sha256=sha(original/'map-splits.json'),split_sha256=sha(root/'map-splits.json'),manifests=[dict(path=str(p),sha256=sha(p)) for p in output_manifests],original_files=original_hashes,limitations=['Initial maps are pure procedural step0 outputs with zero-initialized area masks by source provenance; no direct raw paint-count column exists.','AI seat is exact; no painting inferred from captured_goal_count.','Maxima/Warrush zero-goal rows retained, including blocked painted tiles and truly empty zones whose history is unknown.','Conservative user-accepted approximation, not reconstructed exact paint history.','Same hierarchy weighting policy recomputes weights over retained rows; row weights are not frozen original numerical masses.'])
(root/'DERIVATION.json').write_text(json.dumps(summary,indent=2)+'\n')
command=[sys.executable,str(source/'tools/adaptive_gradient_model.py'),'fit','--split-registry',str(root/'map-splits.json'),'--output',str(root/'filtered-family-research.json'),'--collector-contract','unified-gradient-spatial-h128-v3','--expected-families','materials,guard,clear,forbidden','--threshold-milli','24000']
for path in output_manifests:command.extend(['--manifest',str(path)])
(root/'FIT-COMMAND.json').write_text(json.dumps(dict(argv=command,environment={k:os.environ.get(k) for k in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS']},source=str(source)),indent=2)+'\n')
with (root/'fit.log').open('w') as log:
    process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
    (root/'STATUS.json').write_text(json.dumps(dict(status='FITTING',pid=process.pid,started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),derivation_sha256=sha(root/'DERIVATION.json')),indent=2)+'\n')
    code=process.wait()
if code:
    raise RuntimeError('Filtered fit failed: '+str(code))
assert sha(root/'map-splits.json') == summary['split_sha256']
for path,digest in original_hashes.items():assert sha(Path(path)) == digest
(root/'STATUS.json').write_text(json.dumps(dict(status='FIT-COMPLETE-REVIEW-PENDING',model_sha256=sha(root/'filtered-family-research.json'),finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),originals_and_splits_unchanged=True),indent=2)+'\n')
