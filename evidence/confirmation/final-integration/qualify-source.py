import datetime
import hashlib
import json
from pathlib import Path
import subprocess

OUT = Path(__file__).resolve().parent
P = OUT.parent.parent
WEB = P / 'final-integration'
ROOT = Path('/home/bradley/.codex/worktrees/engine-optimization/glob2')
REV = 'efb843d8047bba6f546c031614a31c917513ad54'
def read(path): return json.loads(path.read_text())
def sha(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == REV
assert not subprocess.check_output(['git', 'status', '--porcelain=v1'], cwd=ROOT, text=True).strip()
build = read(OUT / 'candidate-build.json')
assert all(read(OUT / name)['state'] == 'passed' for name in ['candidate-build.json', 'native-validation.json', 'native-partial-verbose.json'])
assert read(WEB / 'web-complete.json')['state'] == 'qualified_with_explicit_UI_limitation'
rest = read(WEB / 'web-control-restoration.json')
assert rest['state'] == 'restored_verified' and rest['HEAD'] == REV and rest['git_clean']
cache = read(WEB / 'web-control-cache-safety.json')
assert cache['state'] == 'passed' and len(cache['moves']) == 5
assert all(not Path(move['source']).exists() for move in cache['moves'])
for name, expected in build['source_sha256'].items(): assert sha(ROOT / name) == expected
for name, expected in build['binary_sha256'].items(): assert sha(ROOT / 'build/wake-control' / name) == expected
critical = {}
for name in ['index.js', 'index.wasm', 'script-tests.js', 'script-tests.wasm', 'threaded/index.js', 'threaded/index.wasm', 'threaded/script-tests.js', 'threaded/script-tests.wasm']:
    actual = sha(ROOT / 'build/emscripten/client/release' / name)
    assert actual == rest['all_public_files_restored_sha256'][name]
    critical[name] = actual
for browser in ['chromium', 'firefox']:
    assert (WEB / ('web-' + browser) / 'checksums.txt').read_bytes() == (OUT / 'golden-candidate/checksums.txt').read_bytes()
paths = [OUT / name for name in ['control-build.json', 'candidate-build.json', 'environment.json', 'native-validation.json', 'native-partial-verbose.json', 'golden-control-receipt.json']]
paths += [WEB / name for name in ['source-review.json', 'web-build.json', 'web-review.json', 'web-complete.json', 'web-control-restoration.json', 'web-control-cache-safety.json']]
result = {
    'state': 'passed', 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'revision': REV, 'base': build['base'], 'PR': 'https://github.com/Globulation2/glob2/pull/1185',
    'decision': 'Accept bounded wakeups for review on tested workloads; no universal optimality or fixed pool policy.',
    'source_sha256': build['source_sha256'],
    'native': {'unit_cases': 56, 'save_load_cases': 2, 'failed': 0, 'skipped': 0, 'golden_rows': 843, 'partial_verbose_assertions': 54, 'partial_actual_participants': 5},
    'browser_engine': {'Chromium': {'cases': 228, 'assertions': 8117508, 'golden_rows': 843, 'partial_four_workers': True, 'original_UI_wrapper': 'passed'}, 'Firefox': {'cases': 228, 'assertions': 8117508, 'golden_rows': 843, 'partial_four_workers': True, 'original_UI_wrapper': 'failed; same direct corpus passed'}},
    'Firefox_UI_control': 'Same loading/InvalidStateError reproduced with exact original054 executor header and unchanged flags. UI test remains failed; recorded existing limitation.',
    'root_reverified_critical_public_hashes': critical,
    'bound_public_restore_receipt_count': len(rest['all_public_files_restored_sha256']),
    'receipts': [{'path': str(path), 'sha256': sha(path)} for path in paths],
    'reused_exact_core_evidence': {'timing': '256 CPU games,128 endpoint pairs,no caps; independent raw audits retained.', 'parity': '40 traces=20 paired contexts;32CPU+8actualOpenCL runs,small/large/mixed/mature/load/continuations.', 'focused': '202 earlier focused cases acrossB/W,101selectedW;576 bursts across4headers,144selectedlatestW;64Off guards.'},
    'cache_cleanup': 'Control-derived web object directories quarantined, active targets absent. Future SCons must recreate missing targets; tested candidate public outputs are unchanged.',
    'limits': ['No W runtime verification on Windows,macOS,Android,iOS; no second physical CPU or GPU vendor performance replication.', 'Browser engine correctness is not cross-device performance evidence; Firefox UI startup remains failed in both matched roles.', 'No manual native playthrough, quantitative input/frame pacing or sustained thermal/battery validation.', 'Final timing uses CPU backend and two fixed Nicowar maps; mixedAI/GPU correctness and short pilots are separate.']
}
(OUT / 'selected-source-validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({'state': result['state'], 'revision': REV, 'native_cases': 58, 'golden_rows': 843, 'public_restore_count': result['bound_public_restore_receipt_count']}))
