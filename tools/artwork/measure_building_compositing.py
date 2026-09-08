#!/usr/bin/env python3
"""Measure the disposable GIMP audit exports; do not change any runtime artwork."""
import hashlib,json
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
AUDIT=ROOT/'.cache/original-art/compositing-audit'
def rgba(path):return Image.open(path).convert('RGBA')
rows=[]
for frame in ['hosp0b0','defencetower1b0']:
    direct=np.asarray(rgba(AUDIT/(frame+'-direct.png')),dtype=float)
    split=np.asarray(Image.alpha_composite(rgba(AUDIT/(frame+'-base.png')),rgba(AUDIT/(frame+'-team.png'))),dtype=float)
    visible=(direct[:,:,3]>0)|(split[:,:,3]>0)
    delta=np.abs(direct-split)
    assert delta[visible].max()<=3
    rows.append(dict(id=frame,max_rgba_delta=delta[visible].max(axis=0).tolist(),mean_rgba_delta=delta[visible].mean(axis=0).tolist(),pixels_over_one_code_value=int(np.sum(np.any(delta>1,axis=2)))))
classic=rgba(ROOT/'data/gfx/defencetower1b0r.png')
small=rgba(AUDIT/'tower-lowres-team.png')
assert classic.size==small.size
a=np.asarray(classic);b=np.asarray(small)
assert np.array_equal(a[:,:,3],b[:,:,3])
assert np.array_equal(a[a[:,:,3]>0],b[b[:,:,3]>0])
paths=['datasrc/gfx/originals/buildings/hosp/level-1/hopital1.xcf','datasrc/gfx/originals/buildings/defencetower/level-2/tower2.xcf','datasrc/gfx/originals/buildings/defencetower/level-2/tower1.xcf','data/gfx/defencetower1b0r.png']
record=dict(method='GIMP 2.10 saved layer settings: direct visible composite versus separately composited base/team groups',direct_vs_split=rows,low_resolution_tower_team_equals_classic_visible_pixels=True,low_resolution_tower_size=list(small.size),sources=[dict(path=p,sha256=hashlib.sha256((ROOT/p).read_bytes()).hexdigest()) for p in paths])
(ROOT/'datasrc/gfx/provenance/building-compositing-audit.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS: split export differs only by <=3 code values; smaller tower XCF reproduces classic visible team pixels and alpha exactly')
