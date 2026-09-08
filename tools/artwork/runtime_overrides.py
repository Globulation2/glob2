"""Apply complete, hash-verified original-art frames to the experimental pack."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DERIVED = ROOT / 'datasrc/gfx/derived/recovered-v1'
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def apply(records, output):
    replacements = json.loads((DERIVED / 'manifest.json').read_text())
    assert replacements['version'] == 1
    by_id = {f['id']: f for f in records}
    prepared = []
    for frame in replacements['frames']:
        old = by_id[frame['id']]
        assert (old['width'], old['height'], old['scale']) == (frame['width'], frame['height'], frame['scale'])
        roles = [layer['role'] for layer in frame['layers']]
        assert len(set(roles)) == len(roles) and set(roles) <= {'base', 'team'}
        assert {l['role'] for l in old['layers']} <= set(roles), frame['id']
        for source in frame['sources']:
            assert digest(ROOT / source['path']) == source['sha256'], source['path']
        for layer in frame['layers']:
            assert Path(layer['file']).name == layer['file']
            path = DERIVED / layer['file']
            assert digest(path) == layer['sha256'], path
            with Image.open(path) as image:
                assert image.mode == 'RGBA' and image.size == (frame['width']*4, frame['height']*4), path
        prepared.append(copy.deepcopy(frame))
    # Verify every layer before replacing anything; never leave half a frame.
    for frame in prepared:
        for layer in frame['layers']:
            shutil.copyfile(DERIVED / layer['file'], output / layer['file'])
            layer['source_sha256'] = layer['sha256']
        by_id[frame['id']].clear()
        by_id[frame['id']].update(frame)
    return len(prepared)
