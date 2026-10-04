# SPDX-License-Identifier: GPL-3.0-or-later
"""Re-run the QA suite over the listening-verdict corpus (``tools/music/qa/corpus.toml``).

Pipeline role: thresholds in ``spec.py`` are only as good as their calibration. This
module runs every check on each corpus set and prints

* a matrix of check statuses per set, grouped by verdict, and
* the per-measure values behind it, so a threshold change can be justified.

Whenever a threshold or a measure changes, rerun ``python3 -m glob2music calibrate``
and confirm two things: every ``good`` set still passes (warnings allowed), and each
``bad`` set still trips the check that explains its rejection where one exists.

A corpus set that has a ``sets/<id>/set.toml`` is checked exactly as ``check <id>``
would check it: against that set's spec (``manifest.set_spec``, e.g. a loudness
offset) and with that manifest's waivers. Rejected sets have no manifest and are
checked against the default spec with no waivers.

Corpus sets are shipped files under ``data/zik`` or files read from pinned git
commits (``git_ref`` + ``git_path``), extracted on demand into the git-ignored
``tools/music/build/calibration/<id>``. Missing sets are reported and skipped.
"""
from concurrent.futures import ProcessPoolExecutor
import json
from pathlib import Path
import subprocess
import tomllib

from ..manifest import BUILD_DIR, MUSIC_ROOT, REPO_ROOT, SETS_DIR, load_manifest, set_spec
from ..spec import DEFAULT_SPEC, FILENAMES, MOODS
from . import CHECK_NAMES, run_checks

CORPUS = MUSIC_ROOT / 'qa' / 'corpus.toml'
EXTRACT_DIR = BUILD_DIR / 'calibration'
VERDICTS = ('good', 'reference', 'bad')
_LETTER = {'pass': '.', 'warn': 'W', 'fail': 'F', 'waived': 'w', 'skip': '-', 'info': '.'}


def load_corpus(path=CORPUS):
    with open(path, 'rb') as f:
        return tomllib.load(f)['set']


def materialise(entry):
    """Local directory for a corpus entry, extracting git-ref sets if needed."""
    if 'path' in entry:
        return REPO_ROOT / entry['path']
    out = EXTRACT_DIR / entry['id']
    for mood in MOODS:
        dest = out / FILENAMES[mood]
        if not dest.exists():
            out.mkdir(parents=True, exist_ok=True)
            blob = subprocess.run(['git', '-C', str(REPO_ROOT), 'show',
                                   f"{entry['git_ref']}:{entry['git_path']}/{FILENAMES[mood]}"],
                                  capture_output=True, check=True).stdout
            dest.write_bytes(blob)
    return out


def spec_and_waivers(set_id):
    """``(spec, waivers)`` a corpus set is checked with: its own manifest's, if any."""
    if (SETS_DIR / set_id / 'set.toml').exists():
        manifest = load_manifest(set_id)
        return set_spec(manifest), dict(manifest.waivers)
    return DEFAULT_SPEC, {}


def _run(entry):
    try:
        directory = materialise(entry)
    except (subprocess.CalledProcessError, OSError) as e:
        return entry, None, f'unavailable: {e}'
    if not directory.exists():
        return entry, None, f'missing {directory}'
    spec, waivers = spec_and_waivers(entry['id'])
    report = run_checks(directory, spec=spec, waivers=waivers)
    return entry, report.to_dict(), ''


def calibrate(corpus=CORPUS, jobs=4, json_out=None):
    """Run the corpus; print the matrix and measure dump; return the results list."""
    entries = load_corpus(corpus)
    with ProcessPoolExecutor(jobs) as ex:
        results = list(ex.map(_run, entries))
    order = {v: i for i, v in enumerate(VERDICTS)}
    results.sort(key=lambda r: (order.get(r[0]['verdict'], 9), r[0]['id']))
    lines = ['Status matrix (. pass, W warn, F fail, w waived, - skipped):', '']
    head = f"{'set':24s} {'verdict':9s} " + ' '.join(f'{c[:5]:>5s}' for c in CHECK_NAMES)
    lines += [head, '-' * len(head)]
    for entry, rep, err in results:
        if rep is None:
            lines.append(f"{entry['id']:24s} {entry['verdict']:9s} {err}")
            continue
        status = {c['name']: c['status'] for c in rep['checks']}
        cells = ' '.join(f"{_LETTER.get(status.get(c, 'skip'), '?'):>5s}" for c in CHECK_NAMES)
        lines.append(f"{entry['id']:24s} {entry['verdict']:9s} {cells}")
    lines += ['', 'Non-passing measures and why each set was judged as it was:', '']
    for entry, rep, err in results:
        if rep is None:
            continue
        lines.append(f"{entry['id']} ({entry['verdict']}): {entry.get('why', '')}")
        for c in rep['checks']:
            for m in c['measures']:
                if m['status'] in ('warn', 'fail', 'waived'):
                    v = m['value']
                    vs = f'{v:.3f}' if isinstance(v, float) else str(v)
                    lines.append(f"    {m['status']:6s} {m['name']:32s} {vs:>9s}  {m['threshold']}")
        lines.append('')
    lines += ['Judged measure values (min / max over corpus groups):', '']
    groups = {}
    for entry, rep, _ in results:
        if rep is None:
            continue
        for c in rep['checks']:
            for m in c['measures']:
                if isinstance(m['value'], (int, float)) and m['status'] != 'info':
                    key = m['name'].split('.', 1)[0] + '.' + m['name'].rsplit('.', 1)[-1]
                    groups.setdefault(key, {}).setdefault(entry['verdict'], []).append(m['value'])
    for key in sorted(groups):
        parts = []
        for verdict in VERDICTS:
            vals = groups[key].get(verdict)
            if vals:
                parts.append(f'{verdict} {min(vals):.3g}..{max(vals):.3g}')
        lines.append(f'  {key:28s} ' + '; '.join(parts))
    lines += ['', contract_summary(results)]
    text = '\n'.join(lines)
    print(text)
    if json_out:
        Path(json_out).write_text(json.dumps([{'id': e['id'], 'verdict': e['verdict'], 'report': r, 'error': x}
                                              for e, r, x in results], indent=1))
    return results


def contract_summary(results):
    """One line: does the corpus meet the calibration contract (every good and
    reference set passes, warnings allowed; every bad set fails)?"""
    broken = []
    for entry, rep, err in results:
        if rep is None:
            broken.append(f"{entry['id']} not checked ({err})")
        elif entry['verdict'] == 'bad' and rep['status'] != 'fail':
            broken.append(f"{entry['id']} (bad) does not fail")
        elif entry['verdict'] != 'bad' and rep['status'] == 'fail':
            broken.append(f"{entry['id']} ({entry['verdict']}) fails")
    if broken:
        return 'Contract NOT met: ' + '; '.join(broken)
    return f'Contract met: all {len(results)} corpus sets judged as their verdicts require.'
