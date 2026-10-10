# SPDX-License-Identifier: GPL-3.0-or-later
"""GPU-independent portfolio, provenance and completed-evidence contracts."""
import hashlib
import json
import re
from pathlib import Path
import time

from corpus import digest, manifest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
def production_configs():
    # The standalone host currently supports the production 16x16 tile ABI.
    # Fail explicitly if shared descriptors change rather than benchmark a stale copy.
    header = (ROOT / 'src/field/AdaptiveGradientPolicy.h').read_text()
    table = header.split('PLANS{{', 1)[1].split('}};', 1)[0]
    rows = re.findall(r"\{Plan::(\w+),\s*Backend::OpenCL,\s*(\d+),\s*(\d+),\s*(true|false),\s*(true|false),\s*(\d+),\s*(\d+)\}", table)
    if not rows or len(rows) != table.count('Backend::OpenCL'):
        raise ValueError('unsupported production plan descriptor syntax')
    result = {}
    for name, steps, threads, colored, frozen, width, height in rows:
        if (int(width), int(height)) != (16, 16) or name in result:
            raise ValueError('unsupported production tile ABI or duplicate plan')
        result[name] = (int(steps), int(colored == 'true'), int(threads), int(frozen == 'true'))
    return result


CONFIGS = production_configs()
CANDIDATES = ('global', 'frontier', 'bounded')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sources():
    if production_configs() != CONFIGS:
        raise ValueError('production plans changed after process initialization')
    paths = sorted(p for p in HERE.iterdir() if p.suffix in ('.py', '.cpp', '.cl', '.json', '.txt'))
    paths += [ROOT/'src/field/OpenCLGradient.cpp', ROOT/'src/field/AdaptiveGradientPolicy.h',
              ROOT/'requirements-dev.txt']
    return {str(p.relative_to(ROOT)): sha(p) for p in paths}


def load_protocol():
    """Analysis uses the declared protocol, never thresholds edited into a run."""
    return json.loads((HERE / 'protocol.json').read_text())


def finish_run(directory, freeze, records, executions):
    if sources() != freeze['sources']:
        raise ValueError('source changed during qualification')
    receipt = dict(schema=1, freeze_sha256=sha(directory / 'freeze.json'),
                   results_sha256=sha(directory / 'results.jsonl'),
                   layouts=len(freeze['corpus']), records=records, executions=executions,
                   completed_unix=time.time())
    (directory / 'complete.json').write_text(json.dumps(receipt, indent=2))


def validate_completed(directory):
    freeze = json.loads((directory / 'freeze.json').read_text())
    if freeze['protocol'] != load_protocol():
        raise ValueError('frozen protocol differs from the declared protocol')
    if freeze['sources'] != sources():
        raise ValueError('source differs from frozen evidence; use its archived analyzer')
    cases = manifest(freeze['split'])
    if freeze['corpus'] != cases or freeze['corpus_sha256'] != digest(cases):
        raise ValueError('corpus differs from the declared independent-layout roster')
    candidates = freeze.get('candidates')
    if not isinstance(candidates, list) or not candidates or len(set(candidates)) != len(candidates):
        raise ValueError('missing or duplicate frozen candidate roster')
    if any(name not in CANDIDATES for name in candidates):
        raise ValueError('unknown frozen candidate')
    if freeze['split'] != 'final' and candidates != list(CANDIDATES):
        raise ValueError('development/stress must cover the complete candidate portfolio')
    if freeze['split'] == 'final':
        path = directory / 'development-analysis.json'
        if sha(path) != freeze.get('development_report_sha256'):
            raise ValueError('development admission evidence changed')
        prior = json.loads(path.read_text())
        if (prior.get('split') != 'development' or prior.get('sources') != freeze['sources'] or
                prior.get('screen_survivors') != candidates):
            raise ValueError('final candidate roster differs from development survivors')
    if not (directory / 'complete.json').exists():
        raise ValueError('incomplete qualification; no admission or screening claim')
    receipt = json.loads((directory / 'complete.json').read_text())
    if (receipt.get('schema') != 1 or receipt.get('freeze_sha256') != sha(directory / 'freeze.json') or
            receipt.get('results_sha256') != sha(directory / 'results.jsonl') or
            receipt.get('layouts') != len(cases)):
        raise ValueError('completion receipt does not match frozen inputs/results')
    return freeze, receipt


def consume_holdouts(ledger, cases, output):
    """Called after setup, immediately before any final input is generated."""
    ledger.mkdir(parents=True, exist_ok=True)
    with (ledger / (digest(cases) + '.json')).open('x') as receipt:
        json.dump(dict(output=str(output), sources=sources(), consumed_unix=time.time()), receipt, indent=2)
