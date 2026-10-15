#!/usr/bin/env python3
"""Final committed-source integration checks; never measures performance."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

OUT = Path(__file__).resolve().parent
ROOT = Path('/home/bradley/.codex/worktrees/engine-optimization/glob2')
BUILD = ROOT / 'build/wake-control'

def main():
    build = json.loads((OUT / 'candidate-build.json').read_text())
    assert build['state'] == 'passed'
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    assert revision == build['revision']
    assert not subprocess.check_output(['git', 'status', '--porcelain=v1'], cwd=ROOT, text=True).strip()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', GLOB2_ASSET_DIR=str(ROOT))
    for key in ['GLOB2_TEST_SEED', 'GLOB2_TEST_MAX_TICKS', 'GLOB2_CHECKSUM_SIDECAR']:
        env.pop(key, None)
    result = {'state': 'running', 'revision': revision, 'base': build['base'],
              'source_sha256': build['source_sha256'], 'checks': [],
              'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    for kind, filters in [
        ('unit', ['ComputeExecutor/*', 'ReplayStepCounter/*',
                  'TurnHarness/two clients on clean links execute identical turns',
                  'TurnHarness/a restarted client reloads, replays the full log and rejoins']),
        ('engine', ['HiringPipeline/whole game save and resume preserves every publication phase*',
                    'SharedWorkerLifecycle/pending gradient work survives a save and continues at the same deadlines'])]:
        directory, junit = OUT / ('native-' + kind), OUT / ('native-' + kind + '.xml')
        assert not directory.exists() and not junit.exists()
        command = ['python3', 'test/run_tests.py', '--build-dir', str(BUILD), '--binary', kind,
                   '--no-display', '--timeout', '120', '-j', '1', '--artifacts', str(directory),
                   '--junit', str(junit)]
        for name in filters:
            command.extend(['--filter', name])
        with (OUT / ('native-' + kind + '.log')).open('w') as log:
            code = subprocess.call(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        entry = {'kind': kind, 'command': command, 'returncode': code}
        try:
            cases = list(ET.parse(junit).getroot().iter('testcase'))
            entry.update(cases=len(cases), failed=sum(c.find('failure') is not None or c.find('error') is not None for c in cases),
                         skipped=sum(c.find('skipped') is not None for c in cases), names=[c.attrib for c in cases])
            entry['partial_pool_warning'] = any('Requested worker pool unavailable' in f.read_text(errors='replace')
                for f in directory.rglob('*') if f.is_file() and f.suffix in {'.txt', '.log', '.xml', '.json'})
            entry['state'] = 'passed' if code == 0 and entry['cases'] > 0 and not entry['failed'] and not entry['skipped'] and not entry['partial_pool_warning'] else 'failed'
        except Exception as error:
            entry.update(state='failed', error=str(error))
        result['checks'].append(entry)
        (OUT / 'native-validation.json').write_text(json.dumps(result, indent=2) + '\n')
        if entry['state'] != 'passed':
            result['state'] = 'failed'
            (OUT / 'native-validation.json').write_text(json.dumps(result, indent=2) + '\n')
            return 1
    output = OUT / 'golden-candidate'
    assert not output.exists()
    env.update(GLOB2_USER_DIR=str(OUT / 'profile-candidate-golden'), GLOB2_USER_DATA_DIR=str(OUT / 'profile-candidate-golden'))
    command = [str(BUILD / 'src/glob2'), 'match', 'verify', 'test/fixtures/multiplayer/FourSquares1.g2mr',
               '--map-file', 'maps/FourSquares1.map.gz', '--output-dir', str(output), '--compute-threads', '4']
    with (OUT / 'golden-candidate.log').open('w') as log:
        code = subprocess.call(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    verdict = json.loads((output / 'verdict.json').read_text())['verdict']
    trace = (output / 'checksums.txt').read_bytes()
    expected = (ROOT / 'test/fixtures/multiplayer/FourSquares1.verify-trace.txt').read_bytes()
    baseline = (OUT / 'golden-control/checksums.txt').read_bytes()
    okay = code == 0 and verdict == 'verified' and trace == expected == baseline
    result['checks'].append({'kind': 'committed-match', 'command': command, 'returncode': code, 'verdict': verdict,
                             'trace_matches_committed_and_control': trace == expected == baseline,
                             'tick_lines': len(trace.splitlines()), 'checksums_sha256': hashlib.sha256(trace).hexdigest(),
                             'state': 'passed' if okay else 'failed'})
    result['state'] = 'passed' if okay else 'failed'
    result['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (OUT / 'native-validation.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'state': result['state'], 'revision': revision, 'checks': [(c['kind'], c['state']) for c in result['checks']]}))
    return int(not okay)

if __name__ == '__main__':
    raise SystemExit(main())
