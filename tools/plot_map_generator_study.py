#!/usr/bin/env python3
"""Publish a reproducible coverage report using the compiled generator catalog.

Usage: python3 tools/plot_map_generator_study.py path/to/validation.csv
Requires numpy and matplotlib. Run after building map-generator-study.
"""
import collections
import csv
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap
from matplotlib.patches import Patch

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/map-generators'
STUDY=OUT/'study'
BINARY=ROOT/'build/src/MapGeneratorStudy'
catalog=json.loads(subprocess.check_output([BINARY,'--catalog'],text=True))
(STUDY/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
lines=(ROOT/'data/texts.en.txt').read_text().splitlines()
strings={line[1:-1]:lines[i+1] for i,line in enumerate(lines[:-1]) if line.startswith('[') and line.endswith(']')}
names={c['method']:strings[c['nameKey']] for c in catalog}
source=Path(sys.argv[1])
groups=collections.defaultdict(list)
for row in csv.DictReader(source.open()):groups[row['config']].append(row)
summary=[]
for key,rows in groups.items():
    ok=[r for r in rows if r['success']=='1'];m=int(rows[0]['method'])
    record=dict(config=key,method=m,name=names[m],attempts=len(rows),successes=len(ok),all_teams_proxy=sum(r['viable_teams']=='4' for r in ok))
    for field in ['grass_tiles','free','fit4','water_tiles','sand_tiles','shore']:
        values=np.array([100*int(r[field])/int(r['tiles']) for r in ok])
        record[field]=float(values.mean());record[field+'_p5']=float(np.percentile(values,5));record[field+'_p95']=float(np.percentile(values,95))
    assert all(int(r['fit4'])<=int(r['free'])<=int(r['grass_tiles']) for r in ok)
    assert all(sum(int(r[k]) for k in ['grass_tiles','sand_tiles','water_tiles','shore'])==int(r['tiles']) for r in ok)
    if m in [7,8]:assert all(int(r['wheat_tiles'])>0 and int(r['wood_tiles'])>0 for r in ok)
    summary.append(record)
(STUDY/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
with (STUDY/'summary.csv').open('w') as f:
    w=csv.DictWriter(f,summary[0].keys());w.writeheader();w.writerows(summary)
with source.open('rb') as src, (STUDY/'validation.csv.gz').open('wb') as dest:
    with gzip.GzipFile(fileobj=dest,mode='wb',mtime=0,filename='') as compressed:shutil.copyfileobj(src,compressed)
bykey={r['config']:r for r in summary}
fig,axes=plt.subplots(1,2,figsize=(12,6),sharey=True,facecolor='#f8fafc')
for ax,field,title in zip(axes,['grass_tiles','free'],['Pure grass terrain','Immediately buildable tiles']):
    for prefix,offset,color,label in [('lobby-before',-.18,'#afbdc9','PR #237 lobby defaults'),('tuned',.18,'#38835c','Tuned defaults')]:
        vals=[bykey[f'{prefix}-{m}'][field] for m in range(1,9)]
        bars=ax.barh(np.arange(8)+offset,vals,.34,color=color,label=label)
        ax.bar_label(bars,labels=[f'{v:.1f}%' for v in vals],padding=4,fontsize=9)
    ax.set_title(title,fontweight='bold');ax.set_xlim(0,78);ax.set_xlabel('% of map tiles');ax.grid(axis='x',alpha=.15);ax.set_axisbelow(True)
    for sp in ax.spines.values():sp.set_visible(False)
axes[0].set_yticks(range(8),[names[m] for m in range(1,9)]);axes[0].invert_yaxis();axes[0].legend(loc='lower right',fontsize=9)
fig.suptitle('Tuned generator defaults: more building space',fontsize=17,fontweight='bold')
fig.text(.02,.015,'1,000 fixed seed attempts per generator per cohort · 128×128 · 4 teams · means exclude generation failures\nBoth cohorts include the legacy correctness fixes. Failure counts and the previous editor baseline are in RESULTS.md.',fontsize=9,color='#4b5563')
fig.tight_layout(rect=(0,.07,1,.94))
for ext in ['png','svg']:fig.savefig(OUT/f'coverage.{ext}',dpi=160)
plt.close(fig)
svg=OUT/'coverage.svg'
svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines())+'\n')
report=['# Measured generator defaults','','Generated from the compiled catalog and fixed seed results. Settings below are a documentation snapshot; change the C++ definitions, then regenerate this file.','','## Coverage and reliability','','All cohorts use the same 1,000 seed attempts per mode (20001–21000); failed attempts are not replaced. Percentages are means of successful maps. Both prior-setting cohorts include the legacy fixes.','','| Generator | Grass: lobby → tuned | Free tiles: lobby → tuned | Failures: lobby → tuned | All-team start proxy: lobby → tuned |','|---|---:|---:|---:|---:|']
for m in range(1,9):
    a,b=bykey[f'lobby-before-{m}'],bykey[f'tuned-{m}']
    report.append(f"| {names[m]} | {a['grass_tiles']:.1f}% → {b['grass_tiles']:.1f}% | {a['free']:.1f}% → {b['free']:.1f}% | {1000-a['successes']} → {1000-b['successes']} | {a['all_teams_proxy']} → {b['all_teams_proxy']} |")
report+=['','## Previous editor comparison','','The legacy editor normally applies zero sand/desert weights on its first visible slider timer tick. PR #237’s new lobby used constructor weights of 50 for both. The two previous-setting cohorts are intentionally distinct.','','| Generator | Editor free tiles → tuned | Editor failures → tuned |','|---|---:|---:|']
for m in range(1,9):
    a,b=bykey[f'editor-before-{m}'],bykey[f'tuned-{m}']
    report.append(f"| {names[m]} | {a['free']:.1f}% → {b['free']:.1f}% | {1000-a['successes']} → {1000-b['successes']} |")
report+=['','## Shared controls','','| Control | Range | Step | Default |','|---|---|---:|---:|']
def row(c):
    def value(v):return 1<<v if c['powerOfTwo'] else v
    step='×2' if c['powerOfTwo'] else str(c['step'])
    return f"| {strings.get(c['label'],c['label'])} | {value(c['min'])}–{value(c['max'])} | {step} | {value(c['default'])} |"
report += [row(c) for c in catalog[0]['controls']]
report+=['','Terrain weights are relative weights, not percentages of the final map. Lake size and bridge/channel width are algorithm inputs, not exact final tile dimensions.']
for mode in catalog[1:]:
    report+=['',f"## {names[mode['method']]}",'','| Control | Range | Step | Default |','|---|---|---:|---:|']
    report += [row(c) for c in mode['controls'] if c['group']!=3]
(OUT/'RESULTS.md').write_text('\n'.join(report)+'\n')
# Fixed seeds selected before rendering, without replacements.
colors=['#96bb70','#ddc38e','#5c9cbc','#b8c4a3','#e4c34b','#285838','#7f8490','#292936']
seeds=(22001,22002,22003)
profile='glob2-map-gallery'
preview=ROOT/'artifacts/map-generator-validation/previews';preview.mkdir(parents=True,exist_ok=True)
fig,axes=plt.subplots(2,4,figsize=(13,7),facecolor='#f8fafc')
try:
    for m,ax in zip(range(1,9),axes.flat):
        detail,panels=plt.subplots(1,3,figsize=(10,3.6),facecolor='#f8fafc')
        for seed,panel in zip(seeds,panels):
            path=preview/f'{m}-{seed}.txt'
            result=subprocess.run([BINARY,str(m),str(seed),profile,'preset','tuning','dump='+str(path)],cwd=ROOT,text=True,capture_output=True,check=True)
            study=next(x for x in result.stdout.splitlines() if x.startswith('STUDY,')).split(',')
            assert study[3]=='1',(m,seed,'Preview seed failed; do not replace it.')
            grid=np.loadtxt(path,skiprows=1,dtype=int)
            panel.imshow(grid,cmap=ListedColormap(colors),vmin=0,vmax=7,interpolation='nearest');panel.set_title(f'Seed {seed}');panel.axis('off')
            if seed==seeds[0]:ax.imshow(grid,cmap=ListedColormap(colors),vmin=0,vmax=7,interpolation='nearest')
        detail.suptitle(names[m],fontsize=16,fontweight='bold');detail.tight_layout(rect=(0,0,1,.93));detail.savefig(OUT/f'generator-{m}.png',dpi=130);plt.close(detail)
        ax.set_title(names[m],fontweight='bold');ax.axis('off')
finally:shutil.rmtree(Path.home()/('.'+profile),ignore_errors=True)
fig.suptitle('Tuned defaults · same seed 22001 for every generator',fontsize=17,fontweight='bold')
fig.legend(handles=[Patch(color=c,label=n) for c,n in zip(colors,['Grass','Sand','Water','Shore','Wheat','Wood','Stone','Buildings'])],loc='lower center',ncol=8,frameon=False)
fig.tight_layout(rect=(0,.055,1,.94),h_pad=2.5);fig.savefig(OUT/'maps.png',dpi=150);plt.close(fig)
print('Coverage invariants, published results and 24 fixed-seed map previews passed.')
