const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

// Run the compiled engine with the same fixture and orders as the native CI
// lanes. A minimal host supplies CLI arguments without changing the game shell.
test('WebAssembly produces a complete per-tick simulation trace', async ({page}) => {
  const root = path.resolve(__dirname, '../..');
  const fixture = fs.readFileSync(path.join(root, 'games/cross-replay.game'));
  await page.route('**/determinism.html', route => route.fulfill({
    contentType: 'text/html',
    body: `<!doctype html><canvas id="canvas"></canvas><script>
      window.engineLog = [];
      var Module = {
        noInitialRun: true,
        canvas: document.getElementById('canvas'),
        print: message => engineLog.push(String(message)),
        printErr: message => engineLog.push(String(message)),
        preRun: [function() {
          ENV.GLOB2_REPLAY_PATH = '/tmp/wasm.replay';
          ENV.GLOB2_CHECKSUM_SIDECAR = '1';
          FS.writeFile('/tmp/initial.game', Uint8Array.from(atob('${fixture.toString('base64')}'), c => c.charCodeAt(0)));
        }],
        onRuntimeInitialized() {
          Module.callMain(['--nox', '/tmp/initial.game', '1500', '1']);
          window.simulationTrace = Array.from(FS.readFile('/tmp/wasm.replay.checksums'));
        }
      };
    </script><script src="/index.js"></script>`,
  }));
  await page.goto('/determinism.html');
  await page.waitForFunction(() => Array.isArray(window.simulationTrace));
  const trace = Buffer.from(await page.evaluate(() => window.simulationTrace));
  expect(trace.length).toBeGreaterThan(1000);
  const output = path.join(root, 'artifacts/browser-determinism/wasm');
  fs.mkdirSync(output, {recursive: true});
  fs.writeFileSync(path.join(output, 'wasm.replay.checksums'), trace);
  fs.writeFileSync(path.join(output, 'run.log'), (await page.evaluate(() => window.engineLog)).join('\n'));
  fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify({
    fixture: 'games/cross-replay.game', seed: 42, ticks: 1500,
    fixture_sha256: crypto.createHash('sha256').update(fixture).digest('hex'),
    trace_sha256: crypto.createHash('sha256').update(trace).digest('hex'),
  }, null, 2) + '\n');
});
