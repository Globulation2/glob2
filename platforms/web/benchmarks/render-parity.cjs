// Draws renderer parity scenes in Chromium with the browser build of
// torus-render-benchmark (cmake/web.cmake web-render-parity) and saves each
// capture. tools/rendering/browser_parity.py writes the job file, serves the
// release build and compares the captures with the native consensus:
//
//   node platforms/web/benchmarks/render-parity.cjs job.json
//
// job.json: {url, buildDir, runtime, chromiumArgs, jobs, timeoutSeconds,
//            scenes: [{name, renderer, args, env, input, capture, log}]}
// input is {path, file}: a host file the page fetches into the runtime's file
// system at path before main() (a saved game). The runtime writes its capture
// to the file system (GLOB2_BENCH_CAPTURE), which is copied to scene.capture.
const fs = require('node:fs');
const path = require('node:path');
const {randomUUID} = require('node:crypto');
const {chromium} = require('playwright');

const job = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));

function hostPage(scene, inputUrl) {
  const config = {
    args: scene.args, env: scene.env, renderer: 'webgl2',
    input: scene.input ? {path: scene.input.path, url: inputUrl} : null,
    capture: scene.env.GLOB2_BENCH_CAPTURE, runtime: job.runtime,
  };
  const directory = path.posix.dirname(job.runtime);
  // A minimal page: no menu shell, a fresh in-memory profile, the startup
  // asset packages and HD artwork installed before main(), as the game has
  // them once its staged downloads finish.
  return `<!doctype html><meta charset="utf-8"><body style="margin:0;background:#000">
<canvas id="canvas" width="${scene.width}" height="${scene.height}"></canvas>
<script src="/presentation-surface.js"></script>
<script>
const parity = {config: ${JSON.stringify(config)}, lines: []};
window.parity = parity;
const canvas = document.getElementById('canvas');
var Module = {
  noInitialRun: true, renderer: parity.config.renderer, canvas,
  locateFile: name => name.startsWith('assets/') ? '/' + name : '/${directory}/' + name,
  print: line => parity.lines.push(String(line)),
  printErr: line => parity.lines.push(String(line)),
  preRun: [() => {
    ENV.HOME = '/home/web_user';
    FS.mkdirTree('/home/web_user');
    FS.mkdirTree('/tmp/parity');
    for (const [key, value] of Object.entries(parity.config.env)) ENV[key] = value;
    Module.storage = new Glob2Storage(callback => callback(null));
    Module.storageRestore = 'ready';
    Module.storage.restored(null);
    Module.glob2RequireAsset('hd');
    const input = parity.config.input;
    if (input) {
      addRunDependency('parity-input');
      fetch(input.url).then(response => {
        if (!response.ok) throw Error('input HTTP ' + response.status);
        return response.arrayBuffer();
      }).then(bytes => {
        FS.writeFile(input.path, new Uint8Array(bytes));
        removeRunDependency('parity-input');
      }).catch(error => { parity.error = 'input: ' + error; });
    }
  }],
  onRuntimeInitialized: async () => {
    try {
      parity.code = await Module.start(parity.config.args);
      parity.capture = FS.analyzePath(parity.config.capture).exists ? FS.readFile(parity.config.capture) : null;
    } catch (error) { parity.error = String(error); }
    parity.done = true;
  },
  onAbort: reason => { parity.error = 'abort: ' + reason; parity.done = true; },
};
Module.glob2PresentationSurface = Glob2PresentationSurface.create(Module);
Module.glob2PresentationSurface.bind(() => () => {});
const NativeWorker = Worker;
window.Worker = class extends NativeWorker {
  constructor(...args) { super(...args); Module.glob2PresentationSurface.attachWorker(this); }
};
</script>
<script src="/${job.runtime}"></script>`;
}

async function runScene(browser, scene) {
  const started = Date.now();
  const name = 'render-parity-' + randomUUID() + '.html';
  const file = path.join(job.buildDir, name);
  const inputUrl = '/__parity-input/' + randomUUID();
  fs.writeFileSync(file, hostPage(scene, inputUrl));
  const page = await browser.newPage({viewport: {width: scene.width, height: scene.height}, deviceScaleFactor: 1});
  const record = {name: scene.name, renderer: scene.renderer};
  const pageErrors = [];
  page.on('pageerror', error => pageErrors.push(String(error)));
  page.on('console', message => { if (message.type() === 'error') pageErrors.push(message.text()); });
  try {
    if (scene.input)
      await page.route('**' + inputUrl, route => route.fulfill({body: fs.readFileSync(scene.input.file),
        contentType: 'application/octet-stream'}));
    await page.goto(job.url + '/' + name, {waitUntil: 'load'});
    await page.waitForFunction(() => window.parity?.done || window.parity?.error,
      null, {timeout: job.timeoutSeconds * 1000, polling: 1000});
    const result = await page.evaluate(() => {
      const p = window.parity;
      let capture = null;
      if (p.capture) {
        let text = '';
        for (let i = 0; i < p.capture.length; i += 0x8000)
          text += String.fromCharCode.apply(null, p.capture.subarray(i, i + 0x8000));
        capture = btoa(text);
      }
      const gl = document.createElement('canvas').getContext('webgl2');
      const info = gl?.getExtension('WEBGL_debug_renderer_info');
      return {code: p.code, error: p.error, lines: p.lines, capture,
        gpu: gl ? String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER)) : null};
    });
    record.code = result.code;
    record.gpu = result.gpu;
    if (result.error) record.error = result.error;
    fs.writeFileSync(scene.log, result.lines.join('\n') + '\n' +
      (pageErrors.length ? '\n# page errors\n' + pageErrors.join('\n') + '\n' : ''));
    if (result.capture) fs.writeFileSync(scene.capture, Buffer.from(result.capture, 'base64'));
    else record.error = record.error || 'no capture (exit ' + result.code + ')';
  } catch (error) {
    record.error = String(error).slice(0, 500);
    const lines = await page.evaluate(() => window.parity?.lines || []).catch(() => []);
    fs.writeFileSync(scene.log, lines.join('\n') + '\n# harness error\n' + record.error + '\n' + pageErrors.join('\n') + '\n');
  } finally {
    await page.close().catch(() => {});
    fs.rmSync(file, {force: true});
  }
  record.seconds = Math.round((Date.now() - started) / 100) / 10;
  console.log(JSON.stringify(record));
  return record;
}

(async () => {
  const pending = [...job.scenes];
  const results = [];
  // One browser per worker, so a crash or a stuck GPU process costs only its scenes.
  await Promise.all(Array.from({length: Math.max(1, job.jobs)}, async () => {
    while (pending.length) {
      const scene = pending.shift();
      const browser = await chromium.launch({headless: true, args: job.chromiumArgs});
      try { results.push(await runScene(browser, scene)); }
      finally { await browser.close().catch(() => {}); }
    }
  }));
  fs.writeFileSync(job.results, JSON.stringify(results, null, 1) + '\n');
})().catch(error => { console.error(error); process.exitCode = 1; });
