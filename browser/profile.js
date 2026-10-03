// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in local CPU investigation; never a CI timing threshold.
const {chromium} = require('@playwright/test');
const fs = require('node:fs/promises');
const {createHash} = require('node:crypto');
const path = require('node:path');
const os = require('node:os');
const {clickMainMenu, clickCustomGameStart, clickControl, clickListRow} = require('./tests/main-menu');

// CDP's nested sessions cover application, simulation and compute workers too.
async function workerSession(cdp, targetId) {
  const {sessionId} = await cdp.send('Target.attachToTarget', {targetId, flatten: false});
  let sequence = 0;
  const pending = new Map();
  cdp.on('Target.receivedMessageFromTarget', (event) => {
    if (event.sessionId !== sessionId) return;
    const message = JSON.parse(event.message);
    const callback = pending.get(message.id);
    if (!callback) return;
    pending.delete(message.id);
    clearTimeout(callback.timer);
    if (message.error) callback.reject(new Error(JSON.stringify(message.error)));
    else callback.resolve(message.result);
  });
  return {
    send(method, params = {}) {
      const id = ++sequence;
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          pending.delete(id);
          reject(new Error('Worker CDP timeout: ' + method));
        }, 30000);
        pending.set(id, {resolve, reject, timer});
        cdp
          .send('Target.sendMessageToTarget', {sessionId, message: JSON.stringify({id, method, params})})
          .catch((error) => {
            clearTimeout(timer);
            pending.delete(id);
            reject(error);
          });
      });
    },
  };
}

(async () => {
  const url = process.argv[2] || 'http://127.0.0.1:8782/?threads=serial&renderer=webgl2';
  const output = path.resolve(process.argv[3] || 'artifacts/browser-performance');
  const seconds = Number(process.env.GLOB2_PROFILE_SECONDS || 20);
  if (!Number.isFinite(seconds) || seconds <= 0) throw new Error('Profile duration must be positive');
  await fs.mkdir(output, {recursive: true});
  const angle = process.env.GLOB2_CHROMIUM_ANGLE || (process.platform === 'darwin' ? 'metal' : null);
  const browser = await chromium.launch({
    headless: process.env.GLOB2_PROFILE_HEADED !== '1',
    args: angle ? ['--use-angle=' + angle] : [],
  });
  try {
    const browserCDP = await browser.newBrowserCDPSession();
    const viewport = {
      width: Number(process.env.GLOB2_PROFILE_WIDTH || 1200),
      height: Number(process.env.GLOB2_PROFILE_HEIGHT || 900),
    };
    const page = await browser.newPage({viewport});
    const errors = [];
    page.on('pageerror', (error) => errors.push(String(error)));
    const logs = [];
    page.on('console', (message) => logs.push(message.text()));
    await page.goto(url);
    await page.waitForFunction(
      () => globalThis.glob2Diagnostics?.snapshot().screen.includes('MainMenuScreen'),
      null,
      {timeout: 150000},
    );
    console.log(
      'ready',
      await page.evaluate(() => ({executionMode: Module.executionMode, renderer: Module.renderer})),
    );
    if (process.env.GLOB2_PROFILE_FIXTURE) {
      await clickMainMenu(page, 'load');
      const chooser = page.waitForEvent('filechooser');
      await clickControl(page, 'import');
      await (await chooser).setFiles(path.resolve(process.env.GLOB2_PROFILE_FIXTURE));
      await page.waitForFunction(() => glob2Diagnostics.snapshot().import === 'succeeded', null, {
        timeout: 60000,
      });
      await clickListRow(page, 'files', 0);
      await clickControl(page, 'ok');
      await page.waitForFunction(() => glob2Diagnostics.snapshot().screen === 'match', null, {
        timeout: 60000,
      });
    } else {
      await clickMainMenu(page, 'custom');
      await clickCustomGameStart(page);
      await page.waitForFunction(() => glob2Diagnostics.snapshot().tick > 100);
    }
    console.log('match loaded');
    await page.waitForTimeout(4000);
    const cdp = await page.context().newCDPSession(page);
    const profilers = [{id: 'page', session: cdp}];
    for (const target of (await browserCDP.send('Target.getTargets')).targetInfos.filter(
      (t) => t.type === 'worker',
    )) {
      profilers.push({id: target.targetId, session: await workerSession(browserCDP, target.targetId)});
    }
    for (const {id, session} of profilers) {
      await session.send('Profiler.enable');
      await session.send('Profiler.setSamplingInterval', {interval: 1000});
      // Idle pthreads can block in Atomics.wait and cannot service JS evaluation.
      // Profiler commands still work there; instrument submission only on the page.
      if (id !== 'page') continue;
      await session.send('Runtime.evaluate', {
        expression: `
        globalThis.glob2ProfileFrames = [];
        const gl = globalThis.Module?.ctx;
        if (gl) {
          let drew = false;
          for (const name of ['drawArrays','drawElements']) {
            const original = gl[name];
            gl[name] = function(...args) { drew = true; return original.apply(this,args); };
          }
          let proto = gl, descriptor;
          while (proto && !descriptor) { descriptor = Object.getOwnPropertyDescriptor(proto,'drawingBufferWidth'); proto = Object.getPrototypeOf(proto); }
          if (descriptor?.get) Object.defineProperty(gl,'drawingBufferWidth',{get() {
            if (drew) { glob2ProfileFrames.push(performance.now()); drew = false; }
            return descriptor.get.call(this);
          }});
        }`,
      });
    }
    console.log('profiling', profilers.length, 'threads');
    const records = [];
    const requested = (process.env.GLOB2_PROFILE_MODES || 'normal,8x,paused').split(',');
    if (requested.some((mode) => !['normal', '8x', 'paused'].includes(mode)))
      throw new Error('Unknown profile mode');
    const modes = ['normal', '8x', 'paused'].filter((mode) => requested.includes(mode));
    for (const mode of modes) {
      if (mode === '8x')
        for (let i = 0; i < 7; ++i) await page.locator('#canvas').press('Control+=', {delay: 80});
      if (mode === 'paused') await page.locator('#canvas').press('p', {delay: 80});
      await page.waitForTimeout(1000);
      const before = await page.evaluate(() => glob2Diagnostics.snapshot());
      const hostLoadBefore = os.loadavg();
      await cdp.send('Runtime.evaluate', {expression: 'glob2ProfileFrames = []'});
      await Promise.all(profilers.map((p) => p.session.send('Profiler.start')));
      const cpuBefore = (await browserCDP.send('SystemInfo.getProcessInfo')).processInfo;
      const start = performance.now();
      await page.waitForTimeout(seconds * 1000);
      const cpuAfter = (await browserCDP.send('SystemInfo.getProcessInfo')).processInfo;
      const elapsed = performance.now() - start;
      const after = await page.evaluate(() => glob2Diagnostics.snapshot());
      const profiles = await Promise.all(
        profilers.map(async (p) => ({id: p.id, ...(await p.session.send('Profiler.stop'))})),
      );
      const processCPU = cpuAfter.map((p) => ({
        ...p,
        seconds: p.cpuTime - (cpuBefore.find((b) => b.id === p.id)?.cpuTime || p.cpuTime),
      }));
      const hotByThread = [];
      const rendering = [];
      for (const {id, session} of profilers) {
        if (id !== 'page') continue;
        const result = await session.send('Runtime.evaluate', {
          expression: 'glob2ProfileFrames',
          returnByValue: true,
        });
        const times = result.result.value || [];
        if (times.length < 2) continue;
        const intervals = times
          .slice(1)
          .map((t, i) => t - times[i])
          .sort((a, b) => a - b);
        const percentile = (p) => intervals[Math.min(intervals.length - 1, Math.floor(p * intervals.length))];
        rendering.push({
          id,
          frames: times.length,
          fps: (1000 * (times.length - 1)) / (times.at(-1) - times[0]),
          interval_ms: {
            p50: percentile(0.5),
            p95: percentile(0.95),
            p99: percentile(0.99),
            max: intervals.at(-1),
          },
        });
      }
      for (const {id, profile} of profiles) {
        await fs.writeFile(path.join(output, mode + '-' + id + '.cpuprofile'), JSON.stringify(profile));
        const nodes = new Map(profile.nodes.map((n) => [n.id, n]));
        const self = new Map();
        for (let i = 0; i < profile.samples.length; ++i)
          self.set(profile.samples[i], (self.get(profile.samples[i]) || 0) + profile.timeDeltas[i]);
        const hot = [...self]
          .map(([id, us]) => ({
            name: nodes.get(id).callFrame.functionName,
            url: nodes.get(id).callFrame.url,
            ms: us / 1000,
            percent: (100 * us) / (profile.endTime - profile.startTime),
          }))
          .sort((a, b) => b.ms - a.ms)
          .slice(0, 40);
        hotByThread.push({id, hot});
      }
      const record = {
        mode,
        elapsed_ms: elapsed,
        hostLoadBefore,
        hostLoadAfter: os.loadavg(),
        cpu_core_percent: (processCPU.reduce((sum, p) => sum + p.seconds, 0) * 100000) / elapsed,
        processCPU,
        ticks_per_second: ((after.tick - before.tick) * 1000) / elapsed,
        host_match_frames_per_second: ((after.frames - before.frames) * 1000) / elapsed,
        rendering,
        before,
        after,
        hotByThread,
      };
      records.push(record);
      console.log(
        JSON.stringify({
          mode,
          cpu: record.cpu_core_percent,
          tps: record.ticks_per_second,
          rendering,
          hot: hotByThread.map((t) => ({id: t.id, hot: t.hot.slice(0, 8)})),
        }),
      );
    }
    await page.screenshot({path: path.join(output, 'match.png')});
    const gpu = (await browserCDP.send('SystemInfo.getInfo')).gpu;
    const fixture = process.env.GLOB2_PROFILE_FIXTURE;
    const fixtureHash = fixture
      ? createHash('sha256')
          .update(await fs.readFile(fixture))
          .digest('hex')
      : null;
    await fs.writeFile(
      path.join(output, 'summary.json'),
      JSON.stringify(
        {
          url,
          seconds,
          viewport,
          host: {
            platform: os.platform(),
            arch: os.arch(),
            cpu: os.cpus()[0]?.model,
            logicalCPUs: os.cpus().length,
          },
          fixture: fixture || 'generated custom game',
          fixture_sha256: fixtureHash,
          angle,
          gpu,
          browser: browser.version(),
          errors,
          records,
        },
        null,
        2,
      ),
    );
    await fs.writeFile(path.join(output, 'console.txt'), logs.join('\n'));
    if (errors.length) throw new Error(errors.join('\n'));
  } finally {
    await browser.close();
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
