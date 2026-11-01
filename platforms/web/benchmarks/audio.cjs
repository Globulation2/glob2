// SPDX-License-Identifier: GPL-3.0-or-later
// Application queue qualification against a built/served game directory.
// Device loopback/listening is separate: these counters cannot certify hardware.
const { chromium } = require('@playwright/test');
const { Worker } = require('node:worker_threads');
const os = require('node:os');
const fs = require('node:fs');
const path = require('node:path');

const workload = `
  const {workerData} = require('node:worker_threads');
  const end = Date.now() + workerData.seconds * 1000;
  if (workerData.memory) {
    const data = new Float64Array(16 * 1024 * 1024); // 128 MiB per worker
    let passes = 0;
    while (Date.now() < end) {
      for (let i = 0; i < data.length; i += 8) data[i] += Math.sqrt(i + ++passes);
    }
  } else {
    let value = 1;
    while (Date.now() < end) {
      for (let i = 0; i < 10000; i++) value = Math.sqrt(value + i);
    }
  }
`;

async function startMusic(page, url) {
  // Keep the real server's isolation headers; synthesized documents differ in WebKit.
  await page.goto(new URL('music-output.js', url.endsWith('/') ? url : url + '/').href);
  await page.setContent('<button>Start music</button>');
  await page.evaluate(() => {
    window.HEAPU8 = new Uint8Array(8192);
    window.Module = { _malloc: () => 8, _free: () => {}, printErr: console.error };
  });
  await page.addScriptTag({
    content: fs.readFileSync(path.join(__dirname, '../audio.js'), 'utf8'),
  });
  const track = fs.readFileSync(path.join(__dirname, '../../data/zik/original/a1.opus'));
  await page.evaluate((bytes) => {
    Module.glob2Music.send({ type: 'load', index: 0, bytes: Uint8Array.from(bytes).buffer });
    Module.glob2Music.send({ type: 'select', index: 0, value: false });
    document.querySelector('button').onclick = () =>
      Module.glob2Music.volume(255, false, performance.now() * 1000);
  }, Array.from(track));
  await page.click('button');
  await page.waitForFunction(() => Module.glob2Music.status.consumedFrames > 48000);
}

async function main() {
  const url = process.argv[2] || 'http://127.0.0.1:8770';
  const mode = process.argv[3] || 'singlecore';
  const seconds = Number(process.argv[4] || 600);
  if (
    !['singlecore', 'allcore', 'memory'].includes(mode) ||
    !Number.isFinite(seconds) ||
    seconds < 1
  )
    throw new Error('Expected URL, singlecore|allcore|memory, positive finite seconds');

  const browser = await chromium.launch();
  const workers = [];
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', (error) => errors.push(error.message));
    await startMusic(page, url);
    const before = await page.evaluate(() => Module.glob2Music.status);
    const count = mode === 'allcore' ? os.availableParallelism() : mode === 'memory' ? 4 : 2;
    const start = Date.now();
    for (let i = 0; i < count; i++) {
      const worker = new Worker(workload, {
        eval: true,
        workerData: { seconds, memory: mode === 'memory' },
      });
      worker.on('error', (error) => errors.push('CPU competitor: ' + error.message));
      workers.push(worker);
    }

    let after = before;
    let minQueuedFrames = before.queuedFrames;
    // Keep only aggregate evidence so duration does not grow the harness's memory use.
    while (Date.now() - start < seconds * 1000) {
      await new Promise((resolve) => setTimeout(resolve, 1000));
      after = await page.evaluate(() => Module.glob2Music.status);
      minQueuedFrames = Math.min(minQueuedFrames, after.queuedFrames);
    }
    console.log(
      JSON.stringify(
        {
          mode,
          seconds,
          elapsedSeconds: (Date.now() - start) / 1000,
          workers: count,
          cpus: os.availableParallelism(),
          transport: await page.evaluate(() => Module.glob2Music.transport),
          before,
          after,
          minQueuedFrames,
          errors,
        },
        null,
        2,
      ),
    );
    if (
      after.underruns !== before.underruns ||
      after.starvationFrames !== before.starvationFrames ||
      errors.length
    )
      process.exitCode = 1;
  } finally {
    await Promise.all(workers.map((worker) => worker.terminate()));
    await browser.close();
  }
}
main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
