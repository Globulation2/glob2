#!/usr/bin/env python3
"""Publish an explicit bounded evidence inventory using a separate Git index."""
from pathlib import Path
import argparse
import hashlib
import gzip
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('artifacts/marathi-localization')
REF = 'refs/heads/codex/marathi-localization-evidence'

def git(*args, **kwargs):
    return subprocess.check_output(['git', *args], cwd=ROOT, **kwargs).decode().strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint', action='store_true',
                        help='Publish completed native/online evidence while package builds continue.')
    parser.add_argument('--pending-browser', action='store_true',
                        help='Include completed Android evidence while excluding active browser outputs.')
    args = parser.parse_args()
    previous = json.loads((ROOT / BASE / 'evidence-publication.json').read_text())
    files = {Path(name) for name in previous['files'] if (ROOT / name).is_file()}
    if args.checkpoint or args.pending_browser:
        files = {p for p in files if not any(str(p).startswith(str(BASE / x) + '/') for x in ['integration-refresh/native', 'integration-bc8/native', 'integration-bc8/android'])}

    # The inventory describes the payload and is added once after it is
    # regenerated. It cannot hash or count an inherited copy of itself.
    files.discard(BASE / 'published-inventory.json')
    files = {p for p in files if not (p.name.startswith('publication-') and p.suffix == '.log')}

    # Build SDKs, dependency copies, executables, raw external reference pages,
    # environment dumps and profiles are deliberately outside this inventory.
    directories = ['checks', 'android-overlay-review', 'apple-permission-review',
                   'google-responses', 'google-failures', 'reviews', 'previews',
                   'web-screenshots', 'font-diagnostics', 'android',
                   'web-cross-browser-screenshots', 'online-cross-browser-results',
                   'native-render-final', 'native-render-clean671-partial600s',
                   'native-render-clean671',
                   'native-focused-results',
                   'browser-render-final', 'browser-game',
                   'android-rendering', 'android-attempt1-system-ui-anr', 'integration-refresh', 'integration-bc8']
    extensions = {'.json', '.log', '.png', '.txt', '.md', '.xml', '.py', '.cjs', '.js', '.mjs', '.cpp', '.h', '.sh'}
    for directory in directories:
        if (args.checkpoint or args.pending_browser) and directory in {'browser-game', 'browser-render-final'}:
            continue
        if args.checkpoint and directory in {'android', 'android-rendering'}:
            continue
        for path in (ROOT / BASE / directory).rglob('*'):
            relative = path.relative_to(ROOT)
            if any(str(relative).startswith(str(BASE / x) + '/') for x in ['integration-bc8/native-baseline/master-snapshot', 'integration-bc8/caddy-qa/config', 'integration-bc8/caddy-qa/data']):
                continue
            if str(relative).startswith(str(BASE / 'integration-bc8/preparation-kit') + '/') and path.name != 'manifest.json':
                continue
            if (args.checkpoint or args.pending_browser) and relative == BASE / 'integration-bc8/browser-packaging-contracts.log':
                continue
            if path.name.startswith('publication-') and path.suffix == '.log':
                continue
            if (args.checkpoint or args.pending_browser) and any(str(relative).startswith(str(BASE / x) + '/') for x in ['integration-refresh/native', 'integration-bc8/native', 'integration-bc8/android']):
                continue
            if (path.is_file() and (path.suffix in extensions or path.name.endswith(('.json.gz', '.log.gz', '.txt.gz')))
                    and not {'baseline-browser-fixture', 'previous671'} & set(relative.parts)
                    and not {'profiles', 'node_modules', 'cache'} & set(relative.parts)):
                files.add(relative)
    names = ['EVIDENCE.md', 'google-budget.json', 'registration-checks.json',
             'web-visual-review.json', 'private-web-provenance.json',
             'web-cross-browser-provenance.json', 'online-cross-browser.config.cjs',
             'android-build-restart-provenance.json', 'android-recording-reuse-candidates.json',
             'android-final-producer-provenance.json',
             'android-final-head-approval.json',
             'android-final-visual-review.json', 'android-overlay-visual-review.json',
             'android-evidence-gate-recommendation.json', 'android-runtime-cleanup.json',
             'android-initial-producer-provenance.json',
             'android-object-inputs-before-final.json', 'android-object-inputs-after-final.json',
             'android_finalize_producer.py', 'android_finish_build.py', 'cleanup_owned_android.py',
             'android-smoke-adaptation.json', 'native-deps-reuse.json',
             'native-strip-provenance.json',
             'native-focused-build-commands.json',
             'native-focused-provenance.json',
             'native-gate-inputs.json',
             'supplemental-review-summary.json', 'final-verification.json',
             'publish_evidence.py', 'native_qa_runner.py', 'android_validate_release.py',
             'android_smoke_task.py']
    files.add(BASE / 'validated-android-ba890/archive-manifest.json')
    files.add(BASE / 'validated-android-ffbd/archive-manifest.json')
    for name in names:
        path = BASE / name
        if (ROOT / path).is_file(): files.add(path)
    for path in (ROOT / BASE).glob('*.log'):
        if args.checkpoint and path.name.startswith('android-'):
            continue
        if (args.checkpoint or args.pending_browser) and (
                path.name.startswith('browser-release') or path.name.startswith('browser-qa')):
            continue
        files.add(path.relative_to(ROOT))
    if args.checkpoint or args.pending_browser:
        files = {p for p in files if not (
            str(p).startswith(str(BASE / 'browser-game') + '/') or
            (args.checkpoint and str(p).startswith(str(BASE / 'android-rendering') + '/')) or
            (args.checkpoint and str(p).startswith(str(BASE / 'android') + '/')) or
            (p.parent == BASE and p.suffix == '.log' and (
                (args.checkpoint and p.name.startswith('android-')) or p.name.startswith('browser-release') or
                p.name.startswith('browser-qa'))))}
    # Preserve every quantitative result, but publish a bounded selection of
    # matrix images: first/middle/last per font+scale and every inspected page.
    # Actual menu/tutorial/browser/device screenshots are not sampled here.
    matrix = sorted(p for p in files if p.suffix == '.png' and p.parent.name == 'matrix')
    groups = {}
    for path in matrix:
        groups.setdefault((path.parent, path.name.split('-page-')[0]), []).append(path)
    selected = set()
    for group in groups.values():
        selected.update((group[0], group[len(group)//2], group[-1]))
    review_files = list((ROOT / BASE).rglob('visual-inspections.json'))
    review_files += [ROOT / BASE / 'checks/root-native-visual-review.json']
    def inspected(value, parent):
        if isinstance(value, dict):
            for key, child in value.items():
                if key == 'path' and isinstance(child, str) and child.endswith('.png'):
                    path = Path(child)
                    path = path if path.is_absolute() else (ROOT / path if child.startswith('artifacts/') else parent / path)
                    if path.is_file(): selected.add(path.relative_to(ROOT))
                else: inspected(child, parent)
        elif isinstance(value, list):
            for child in value: inspected(child, parent)
    for review in review_files:
        if review.is_file(): inspected(json.loads(review.read_text()), review.parent)
    files.difference_update(matrix)
    files.update(selected)
    image_manifest = BASE / 'matrix-image-inventory.json'
    (ROOT / image_manifest).write_text(json.dumps({
        'policy': 'All quantitative reports retained. First/middle/last image per font+scale and every explicitly inspected image published; all other full matrix PNGs remain local. Game/browser/device screenshots are unsampled.',
        'images': {str(p): {'sha256': hashlib.sha256((ROOT/p).read_bytes()).hexdigest(),
                           'bytes': (ROOT/p).stat().st_size, 'published': p in selected}
                   for p in matrix}}, indent=2) + '\n')
    files.add(image_manifest)
    compressed = {}
    for path in sorted(files):
        original = ROOT / path
        if path.suffix == '.json' and original.stat().st_size > 90 * 1024**2:
            packed = path.with_suffix(path.suffix + '.gz')
            contents = original.read_bytes()
            (ROOT / packed).write_bytes(gzip.compress(contents, compresslevel=6, mtime=0))
            compressed[str(path)] = {'published_as': str(packed),
                'original_sha256': hashlib.sha256(contents).hexdigest(),
                'original_bytes': len(contents),
                'reason': 'Lossless compression to stay below GitHub single-file size limits.'}
            files.remove(path)
            files.add(packed)
    inventory = sorted(map(str, files))
    size = sum((ROOT / name).stat().st_size for name in inventory)
    hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
              for name in inventory}
    manifest = BASE / 'published-inventory.json'
    (ROOT / manifest).write_text(json.dumps({'source_revision': git('rev-parse', 'HEAD'),
          'payload_bytes': size, 'sha256': hashes, 'compressed_reports': compressed}, ensure_ascii=False, indent=2) + '\n')
    inventory.append(str(manifest))
    total_bytes = size + (ROOT / manifest).stat().st_size
    assert total_bytes <= 512 * 1024**2, f'Evidence exceeds 512 MiB publication budget: {total_bytes}'
    index = ROOT / BASE / 'evidence.index'
    index.unlink(missing_ok=True)
    env = dict(os.environ, GIT_INDEX_FILE=str(index))
    parent = git('rev-parse', REF)
    # Parent links retain history; only the explicit current inventory belongs
    # in the new tree. Exclusions must not leave inherited files behind.
    git('read-tree', '--empty', env=env)
    for offset in range(0, len(inventory), 100):
        git('add', '-f', '--', *inventory[offset:offset + 100], env=env)
    tree = git('write-tree', env=env)
    entries = [entry for entry in git('ls-tree', '-rlz', tree).split('\0') if entry]
    actual_files = {entry.partition('\t')[2] for entry in entries}
    actual_bytes = sum(int(entry.partition('\t')[0].split()[3]) for entry in entries)
    assert actual_files == set(inventory), 'Prepared Git tree differs from explicit evidence inventory'
    assert actual_bytes == total_bytes and actual_bytes <= 512 * 1024**2
    commit = git('commit-tree', tree, '-p', parent, '-m',
                 'Refresh Marathi reviews and final execution evidence', env=env)
    git('update-ref', REF, commit, parent)
    publication = {'revision': git('rev-parse', 'HEAD'), 'evidence_commit': commit,
                   'bytes': total_bytes, 'files': inventory}
    (ROOT / BASE / 'evidence-publication.json').write_text(
        json.dumps(publication, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({key: publication[key] for key in ('revision', 'evidence_commit', 'bytes')}))
    print('Prepared evidence commit; push the evidence branch explicitly after checking it.')

if __name__ == '__main__': main()
