#!/usr/bin/env python3
"""Execute the separate iOS scripting harness and export its own evidence container."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = 'org.globulation2.glob2.script-tests'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--device', required=True, help='Explicit simulator UUID or physical device identifier')
    parser.add_argument('--simulator-set', type=Path, help='Owned simulator set; omit for a physical device')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    app, output = args.app.resolve(), args.output.resolve()
    if not app.is_dir():
        raise ValueError('Build the separate --script-tests app first')
    output.mkdir(parents=True, exist_ok=False)
    run_id = uuid.uuid4().hex
    manifest = {'runId': run_id, 'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
                'device': args.device, 'bundleId': BUNDLE, 'commands': [],
                'executableSha256': hashlib.sha256((app / 'Glob2ScriptTests').read_bytes()).hexdigest(),
                'fixtureHashes': {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in sorted((ROOT / 'test/fixtures/javascript').rglob('*')) if p.is_file()}}

    def command(parts, capture=False):
        manifest['commands'].append(parts)
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        with (output / 'runner.log').open('a') as log:
            return subprocess.check_output(parts, text=True, stderr=log).strip() if capture else subprocess.run(parts, stdout=log, stderr=subprocess.STDOUT, check=True)

    if args.simulator_set:
        base = ['xcrun', 'simctl', '--set', str(args.simulator_set.resolve())]
        command(base + ['install', args.device, str(app)])
        # A finished harness remains open to display its result. Relaunch a new
        # process and remove only its previous evidence to avoid a stale pass.
        try:
            command(base + ['terminate', args.device, BUNDLE])
        except subprocess.CalledProcessError:
            pass  # The separate harness may not be running yet.
        container = Path(command(base + ['get_app_container', args.device, BUNDLE, 'data'], True))
        source = container / 'Documents/ScriptingEvidence'
        if source.exists():
            shutil.rmtree(source)
        command(base + ['launch', args.device, BUNDLE, '--glob2-script-run=' + run_id])
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline:
            result = source / 'result.json'
            if result.is_file() and json.loads(result.read_text()).get('runId') == run_id:
                break
            time.sleep(1)
        if source.exists():
            shutil.copytree(source, output / 'evidence')
    else:
        base = ['xcrun', 'devicectl', 'device']
        command(base + ['install', 'app', '--device', args.device, str(app)])
        command(base + ['process', 'launch', '--terminate-existing', '--device', args.device, BUNDLE, '--glob2-script-run=' + run_id])
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline:
            # Copy only this harness's Documents; no normal game saves are read.
            parts = base + ['copy', 'from', '--device', args.device,
                            '--domain-type', 'appDataContainer', '--domain-identifier', BUNDLE,
                            '--source', 'Documents/ScriptingEvidence', '--destination', str(output / 'evidence')]
            try:
                if (output / 'evidence').exists():
                    shutil.rmtree(output / 'evidence')
                command(parts)
                result = output / 'evidence/result.json'
                if result.is_file() and json.loads(result.read_text()).get('runId') == run_id:
                    break
            except subprocess.CalledProcessError:
                pass
            time.sleep(5)
    result = output / 'evidence/result.json'
    if not result.is_file():
        raise RuntimeError('Harness did not finish; retained available logs and artifacts')
    summary = json.loads(result.read_text())
    if summary.get('runId') != run_id:
        raise RuntimeError('Harness did not finish this run; rejected stale evidence')
    manifest['result'] = summary
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    raise SystemExit(summary['exitCode'])


if __name__ == '__main__':
    main()
