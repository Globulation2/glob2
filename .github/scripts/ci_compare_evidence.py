"""Validate selected evidence identities before comparing complete per-tick traces."""
import argparse
import json
from pathlib import Path
import subprocess


def validate_traces(root, native, browsers):
    reference = None
    for platform in native:
        files = list((root / f'browser-determinism-{platform}').rglob('native.replay.checksums'))
        if len(files) != 1:
            raise ValueError(f'{platform}: expected one native trace, got {files}')
        value = files[0].read_bytes()
        if len(value) <= 1000:
            raise ValueError(f'{platform}: incomplete trace')
        if reference is not None and value != reference:
            raise ValueError(f'{platform}: simulation differs')
        reference = value
    expected = {(browser, variant, threads) for browser in browsers
                for variant, threads in [('serial', 1), ('threaded', 1), ('threaded', 2), ('threaded', 4)]}
    found = set()
    for path in root.glob('browser-determinism-wasm-*/wasm/*/manifest.json'):
        manifest = json.loads(path.read_text())
        identity = (manifest['project'], manifest['variant'], manifest['threads'])
        if identity not in expected or identity in found:
            raise ValueError(f'Unexpected or repeated browser trace: {identity}')
        if manifest['ticks'] != 1500 or manifest['seed'] != 42:
            raise ValueError(f'Unexpected simulation scenario: {path}')
        trace = (path.parent / 'wasm.replay.checksums').read_bytes()
        import hashlib
        if hashlib.sha256(trace).hexdigest() != manifest['trace_sha256'] or trace != reference:
            raise ValueError(f'Browser simulation differs: {identity}')
        found.add(identity)
    if found != expected:
        raise ValueError(f'Missing browser/thread traces: {sorted(expected - found)}')
    return len(native) + len(found)


def native_composition_manifest(root, platform, family):
    manifests = list((root / f'browser-determinism-{platform}').glob(f'**/{family}/native/manifest.json'))
    if len(manifests) != 1:
        raise ValueError(f'{platform}: expected one {family} composition manifest')
    return manifests[0]


def validate_resource_compositions(root, native, browsers, committed, *, units=False,
                                   expected_source=None):
    """Require every selected producer and its complete composition trace."""
    family = 'units' if units else 'resources'
    selection = 'unit-composition' if units else 'composition'
    trace_name = 'unit-ablations.trace' if units else 'seeded-compositions.trace'
    reference = committed.read_bytes().replace(b'\r\n', b'\n')
    if len(reference.splitlines()) != (97 if units else 150):
        raise ValueError(f'Incomplete committed {family} composition trace')
    source = expected_source
    count = 0

    def check(directory, manifest):
        nonlocal source, count
        producer = manifest['producer']
        identity = (producer['revision'], producer['sourceTreeSha256'])
        if producer['dirty'] is not False or not all(identity) or (source is not None and identity != source):
            raise ValueError(f'Resource composition source mismatch: {directory}')
        source = identity
        traces = list(directory.rglob(trace_name))
        if len(traces) != 1 or traces[0].read_bytes().replace(b'\r\n', b'\n') != reference:
            raise ValueError(f'Missing, incomplete or different resource composition: {directory}')
        count += 1

    for platform in native:
        manifest_path = native_composition_manifest(root, platform, family)
        check(manifest_path.parent, json.loads(manifest_path.read_text()))
    expected = {(variant, browser) for variant in ('serial', 'threaded') for browser in browsers}
    found = set()
    for path in root.glob(f'browser-determinism-wasm-*/resources/*/*/{selection}/manifest.json'):
        manifest = json.loads(path.read_text())
        identity = (manifest['variant'], manifest['browser'])
        if identity not in expected or identity in found:
            raise ValueError(f'Unexpected or repeated resource composition: {identity}')
        if manifest['selection']['name'] != selection or manifest.get('exit') != 0 or manifest.get('error'):
            raise ValueError(f'Failed resource composition: {path}')
        check(path.parent, manifest)
        found.add(identity)
    if found != expected:
        raise ValueError(f'Missing browser resource compositions: {sorted(expected - found)}')
    return count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--traces', type=Path, required=True)
    parser.add_argument('--scripting', type=Path, required=True)
    parser.add_argument('--compatibility', choices=['true','false'], required=True)
    parser.add_argument('--variants', choices=['true','false'], required=True)
    parser.add_argument('--macos', choices=['true','false'], default='false')
    args = parser.parse_args()
    native = ['ubuntu-24.04', 'windows']
    profiles = ['javascript-profile-linux-g++-13', 'javascript-profile-mingw']
    if args.compatibility == 'true':
        native.append('ubuntu-22.04')
        profiles.append('javascript-profile-linux-g++-11')
    if args.variants == 'true':
        profiles.append('javascript-profile-linux-clang-18')
    if args.macos == 'true':
        native.append('macos')
        profiles.append('javascript-profile-macos')
    browsers = {'chromium', 'firefox', 'webkit'}
    print(f'{validate_traces(args.traces, native, browsers)} exact native/browser traces match')
    resource_count = validate_resource_compositions(args.traces, native, browsers,
        Path('test/fixtures/resources/seeded-compositions-units.trace'))
    print(f'{resource_count} exact native/browser resource composition traces match')
    resource_producer = json.loads(native_composition_manifest(
        args.traces, native[0], 'resources').read_text())['producer']
    resource_source = (resource_producer['revision'], resource_producer['sourceTreeSha256'])
    unit_count = validate_resource_compositions(args.traces, native, browsers,
        Path('test/fixtures/unit-catalog/ablation-checksums.txt'), units=True,
        expected_source=resource_source)
    print(f'{unit_count} exact native/browser unit composition traces match')
    corpora = []
    for profile in profiles:
        path = args.scripting / profile / 'javascript-corpus'
        if not (path / 'manifest.json').is_file():
            raise ValueError(f'Missing native scripting evidence: {profile}')
        corpora.append(path)
    browser_corpora = [path.parent for path in args.traces.glob('*/script-corpus*/*/manifest.json')]
    expected = {(variant, browser) for variant in ('script-corpus','script-corpus-threaded') for browser in browsers}
    actual = [(path.parent.name, path.name) for path in browser_corpora]
    if len(actual) != len(expected) or set(actual) != expected:
        raise ValueError(f'Expected exact serial/threaded browser scripting corpus: {actual}')
    for index, candidate in enumerate(corpora[1:] + browser_corpora):
        subprocess.run(['python3', 'test/check_javascript_evidence.py', str(corpora[0]), str(candidate),
                        '--output', f'artifacts/javascript-ci/comparison-{index}.json'], check=True)


if __name__ == '__main__':
    main()
