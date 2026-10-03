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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--traces', type=Path, required=True)
    parser.add_argument('--scripting', type=Path, required=True)
    parser.add_argument('--compatibility', choices=['true','false'], required=True)
    parser.add_argument('--variants', choices=['true','false'], required=True)
    args = parser.parse_args()
    native = ['ubuntu-24.04', 'windows']
    profiles = ['javascript-profile-linux-g++-13', 'javascript-profile-mingw']
    if args.compatibility == 'true':
        native.append('ubuntu-22.04')
        profiles.append('javascript-profile-linux-g++-11')
    if args.variants == 'true':
        profiles.append('javascript-profile-linux-clang-18')
    browsers = {'chromium', 'firefox', 'webkit'}
    print(f'{validate_traces(args.traces, native, browsers)} exact native/browser traces match')
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
