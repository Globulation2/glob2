#!/usr/bin/env python3
"""Compare a refactor study to the frozen #238 cohort and render review maps.

Requires numpy and matplotlib. The compiled catalog supplies names and generator
membership. Input CSVs are produced by map_generator_study.py.
"""
import argparse
import csv
import gzip
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap
from matplotlib.patches import Patch
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
COLORS = ['#96bb70', '#ddc38e', '#5c9cbc', '#b8c4a3', '#e4c34b', '#285838', '#7f8490', '#292936',
          '#c2413b', '#3fa69a']
LABELS = ['Grass', 'Sand', 'Water', 'Shore', 'Wheat', 'Wood', 'Stone', 'Buildings', 'Fruit', 'Algae']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'artifacts/map-generators/refactor')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT/'build/src/MapGeneratorStudy'
    catalog = json.loads(subprocess.check_output([binary, '--catalog'], text=True))
    methods = [c for c in catalog if not c.get('editorOnly', c['method'] == 0)]
    frozen_catalog = args.baseline.parent/'catalog.json'
    if frozen_catalog.exists():
        frozen = {d['method']: d for d in json.loads(frozen_catalog.read_text())}
        keys = ('label', 'min', 'max', 'step', 'default', 'group', 'powerOfTwo')
        for definition in catalog:
            if definition['method'] in frozen:
                assert [[c[k] for k in keys] for c in definition['controls']] == [[c[k] for k in keys] for c in frozen[definition['method']]['controls']], f"Controls changed for {definition['id']}"
    lines = (ROOT/'data/texts.en.txt').read_text().splitlines()
    strings = {line[1:-1]: lines[i+1] for i, line in enumerate(lines[:-1]) if line.startswith('[') and line.endswith(']')}
    rows = list(csv.DictReader(args.csv.open()))
    baseline = {r['method']: r for r in json.loads(args.baseline.read_text()) if r['config'].startswith('tuned-')}
    summary = []
    for definition in methods:
        method = definition['method']
        cohort = [r for r in rows if int(r['method']) == method]
        if not cohort:
            continue
        ok = [r for r in cohort if r['success'] == '1']
        assert ok, f'No successful maps for {definition["id"]}'
        record = dict(method=method, id=definition['id'], revision=definition['revision'],
                      name=strings[definition['nameKey']], attempts=len(cohort), successes=len(ok),
                      all_teams_proxy=sum(int(r['viable_teams']) == next(c['default'] for c in definition['controls'] if c['id']=='teams') for r in ok))
        for key in ('grass_tiles', 'free', 'fit4', 'water_tiles', 'sand_tiles', 'shore'):
            values = np.array([100*int(r[key])/int(r['tiles']) for r in ok])
            record[key] = float(values.mean())
            record[key+'_p5'], record[key+'_p95'] = map(float, np.percentile(values, [5, 95]))
        assert all(int(r['fit4']) <= int(r['free']) <= int(r['grass_tiles']) for r in ok)
        assert all(sum(int(r[k]) for k in ('grass_tiles', 'sand_tiles', 'water_tiles', 'shore')) == int(r['tiles']) for r in ok)
        record['review_flags'] = []
        if method in baseline:
            before = baseline[method]
            record['free_delta_pp'] = record['free']-before['free']
            record['failure_delta_pp'] = 100*(1-record['successes']/record['attempts'])-100*(1-before['successes']/before['attempts'])
            record['proxy_delta_pp'] = 100*record['all_teams_proxy']/record['attempts']-100*before['all_teams_proxy']/before['attempts']
            for flag, triggered in [('buildable area', abs(record['free_delta_pp']) > 2),
                                    ('generation failures', record['failure_delta_pp'] > 1+1e-9),
                                    ('start proxy', record['proxy_delta_pp'] < -3-1e-9)]:
                if triggered:
                    record['review_flags'].append(flag)
        summary.append(record)
    (out/'catalog.json').write_text(json.dumps(catalog, indent=2)+'\n')
    (out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    with args.csv.open('rb') as src, (out/'validation.csv.gz').open('wb') as dest:
        with gzip.GzipFile(fileobj=dest, mode='wb', mtime=0, filename='') as zipped:
            shutil.copyfileobj(src, zipped)
    fig, axes = plt.subplots(1, 2, figsize=(13, 6), sharey=True)
    for ax, key, title in zip(axes, ('grass_tiles', 'free'), ('Pure grass', 'Immediately buildable tiles')):
        for offset, color, label, values in [(-.18, '#a6b4c4', 'PR #238', [baseline.get(r['method'], {}).get(key, float('nan')) for r in summary]),
                                             (.18, '#38835c', 'Modular generators', [r[key] for r in summary])]:
            bars = ax.barh(np.arange(len(summary))+offset, values, .34, color=color, label=label)
            ax.bar_label(bars, labels=[f'{v:.1f}%' if np.isfinite(v) else '' for v in values], padding=4, fontsize=8)
        ax.set_title(title)
        ax.set_xlim(0, 76)
        ax.set_xlabel('% of tiles; successful maps')
        ax.spines[['top', 'right']].set_visible(False)
        ax.grid(axis='x', alpha=.15)
        ax.set_axisbelow(True)
    axes[0].set_yticks(range(len(summary)), [r['name'] for r in summary])
    axes[0].invert_yaxis()
    axes[1].legend(loc='lower right')
    fig.suptitle('Map generator refactor — default map character and building space')
    fig.tight_layout()
    for ext in ('png', 'svg'):
        fig.savefig(out/f'coverage.{ext}', dpi=160)
    plt.close(fig)
    # Three preselected seeds plus the weakest successful map by the start proxy.
    preview_records = []
    profile = 'glob2-refactor-gallery'
    try:
        for record in summary:
            method = record['method']
            ok = [r for r in rows if int(r['method']) == method and r['success'] == '1']
            worst = min(ok, key=lambda r: (int(r['viable_teams']), int(r['min_local_fit4']), int(r['free'])))
            seeds = [22001, 22002, 22003, int(worst['seed'])]
            fig, axes = plt.subplots(1, 4, figsize=(12, 3.7))
            for index, (seed, ax) in enumerate(zip(seeds, axes)):
                dump = out/f'.preview-{method}-{seed}.txt'
                result = subprocess.run([binary, str(method), str(seed), profile, 'preset', 'tuning', 'dump='+str(dump)],
                                        cwd=ROOT, text=True, capture_output=True, timeout=12, check=True)
                study = next(line for line in result.stdout.splitlines() if line.startswith('STUDY,')).split(',')
                success = study[3] == '1'
                grid = np.loadtxt(dump, skiprows=1, dtype=int)
                dump.unlink()
                ax.imshow(grid, cmap=ListedColormap(COLORS), vmin=0, vmax=len(COLORS) - 1, interpolation='nearest')
                ax.set_title(f'Seed {seed}'+('\nWeak start example' if index == 3 else '')+(' · FAILED' if not success else ''), fontsize=10)
                ax.axis('off')
                preview_records.append(dict(method=method, seed=seed, success=success, hash=study[-1], role='weak-start' if index == 3 else 'fixed'))
            fig.suptitle(record['name'], fontweight='bold')
            fig.legend(handles=[Patch(color=c, label=l) for c, l in zip(COLORS, LABELS)], loc='lower center', ncol=len(COLORS), frameon=False, fontsize=8)
            fig.tight_layout(rect=(0, .06, 1, .94))
            fig.savefig(out/f'generator-{method}.png', dpi=145)
            plt.close(fig)
    finally:
        shutil.rmtree(Path.home()/('.'+profile), ignore_errors=True)
    (out/'previews.json').write_text(json.dumps(preview_records, indent=2)+'\n')
    paths = list((ROOT/'src/map/generator').rglob('*.h')) + list((ROOT/'src/map/generator').rglob('*.cpp'))
    paths += [ROOT/'src/map/MapTerrain.cpp', ROOT/'test/MapGeneratorStudy.cpp']
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}
    (out/'source-hashes.json').write_text(json.dumps(hashes, indent=2)+'\n')
    report = ['# Modular generator validation', '',
              '1,000 seeds per playable generator (20001–21000), 128×128, four colonies and four workers. Failed attempts are retained; tile percentages exclude failures. The baseline is the frozen tuned cohort from PR #238.', '',
              '![Coverage](coverage.png)', '',
              '| Generator | Free tiles: #238 → refactor | Failures / 1,000 | All-team start proxy / 1,000 | Review flags |',
              '|---|---:|---:|---:|---|']
    for r in summary:
        b = baseline.get(r['method'])
        before_free = f"{b['free']:.2f}%" if b else '—'
        before_failed = str(b['attempts']-b['successes']) if b else '—'
        before_proxy = str(b['all_teams_proxy']) if b else '—'
        report.append(f"| {r['name']} | {before_free} → {r['free']:.2f}% | {before_failed} → {r['attempts']-r['successes']} | {before_proxy} → {r['all_teams_proxy']} | {', '.join(r['review_flags']) or ('None' if b else 'New generator; no baseline')} |")
    report += ['', 'Review thresholds: absolute mean free-area shift >2 percentage points; failure-rate increase >1 point; all-team proxy decrease >3 points. Flags require investigation, not automatic retuning. The start proxy measures nearby building anchors and reachable wheat/wood; it is not a fairness guarantee.', '',
               '[Investigation and visual assessment](REVIEW.md) · [Raw data](validation.csv.gz) · [Catalog](catalog.json)', '', '## Map previews', '',
               'Each row shows three fixed seeds and a weak successful start. Failed fixed seeds, if any, are labeled rather than replaced.']
    for r in summary:
        report += ['', f"### {r['name']}", '', f"![{r['name']}](generator-{r['method']}.png)"]
    (out/'RESULTS.md').write_text('\n'.join(report)+'\n')
    print(json.dumps({r['name']: r['review_flags'] for r in summary}, indent=2))


if __name__ == '__main__':
    main()
