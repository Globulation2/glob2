#!/usr/bin/env python3
"""Compare native and Chromium/Wasm simulation checkpoints in private profiles."""
import argparse
import functools
import http.server
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--web', type=Path, default=ROOT/'build/emscripten/client/release')
    parser.add_argument('--steps', type=int, default=100000)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.steps <= 1000000:
        parser.error('steps must be between 1 and 1000000')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='profile-', dir=output) as profile:
        env = dict(os.environ, GLOB2_USER_DATA_DIR=profile)
        native = subprocess.run([str(args.native.resolve()), 'maps/balanced.map', str(args.steps)],
                                cwd=ROOT, env=env, text=True, capture_output=True, timeout=900)
    (output/'native.log').write_text(native.stdout + native.stderr)
    native.check_returncode()
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *unused):
            pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(args.web.resolve())))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    script = r'''
const {chromium} = require('./browser/node_modules/playwright');
(async () => {
 const browser = await chromium.launch({headless:true});
 try {
  const page = await browser.newPage();
  await page.goto(process.env.GLOB2_HARNESS_URL, {waitUntil:'commit',timeout:60000});
  await page.waitForFunction(() => window.Module && Module.done, null, {timeout:900000});
  const result = await page.evaluate(() => ({result:Module.result,lines:Module.results,elapsed:Module.elapsed}));
  process.stdout.write(JSON.stringify(result));
 } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
'''
    try:
        env = dict(os.environ, TMPDIR=str(output), GLOB2_HARNESS_URL=f'http://127.0.0.1:{server.server_port}/mobile-determinism.html?steps={args.steps}')
        web = subprocess.run(['node', '-e', script], cwd=ROOT, env=env, text=True, capture_output=True, timeout=960)
        (output/'browser-driver.log').write_text(web.stderr)
        web.check_returncode()
        result = json.loads(web.stdout)
        (output/'wasm.log').write_text('\n'.join(result['lines'])+'\n')
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    if result.get('result') not in (None, 0):
        raise RuntimeError('Wasm harness failed; see wasm.log')
    def checkpoints(lines):
        return [line for line in lines if line.startswith('GLOB2_DETERMINISM ')]
    left, right = checkpoints(native.stdout.splitlines()), checkpoints(result['lines'])
    expected = 1 + args.steps//1000 + (args.steps%1000 != 0)
    if len(left) != expected or left != right:
        mismatch = next((i for i, pair in enumerate(zip(left, right)) if pair[0] != pair[1]), None)
        raise RuntimeError(f'Determinism mismatch: native={len(left)}, wasm={len(right)}, expected={expected}, first mismatch={mismatch}; see logs')
    report = {'status':'passed', 'steps':args.steps, 'checkpoints':len(left),
              'native_platform':platform.platform(), 'native_machine':platform.machine(),
              'fixture':'maps/balanced.map', 'seed':42, 'ai':'castor',
              'native_performance':[x for x in native.stdout.splitlines() if x.startswith('GLOB2_PERF ')],
              'wasm_performance':[x for x in result['lines'] if x.startswith('GLOB2_PERF ')]}
    if not report['native_performance'] or not report['wasm_performance']:
        raise RuntimeError('Missing completed performance report')
    (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
