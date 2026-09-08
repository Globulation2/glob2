const {test, expect} = require('@playwright/test');
const {spawn} = require('node:child_process');
const {createInterface} = require('node:readline');
const {mkdtemp, rm} = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {randomUUID} = require('node:crypto');

const root = path.resolve(__dirname, '../..');
const platform = os.platform() === 'win32' ? 'windows' : os.platform();
let lobby, gateway, work, profile, endpoint;
async function start(binary, args, ready) {
  const child = spawn(binary, args, {cwd: work, stdio: ['ignore', 'pipe', 'pipe']});
  try {
    const line = await new Promise((resolve, reject) => {
      const lines = createInterface({input: child.stdout});
      const timer = setTimeout(() => reject(new Error('Server readiness timed out')), 15000);
      let errors = '';
      child.stderr.on('data', chunk => { errors = (errors + chunk).slice(-4000); });
      child.once('error', reject);
      child.once('exit', code => reject(new Error(`Server exited ${code}: ${errors}`)));
      lines.on('line', line => { if (ready(line)) { clearTimeout(timer); lines.close(); resolve(line); } });
    });
    return {child, line};
  } catch (error) { child.kill(); throw error; }
}
async function stop(child) {
  if (!child || child.exitCode !== null) return;
  await new Promise(resolve => { child.once('exit', resolve); child.kill(); });
}

test.beforeAll(async ({baseURL}) => {
  work = await mkdtemp(path.join(os.tmpdir(), 'glob2-yog-'));
  profile = 'glob2-yog-test-' + randomUUID();
  lobby = (await start(path.join(root, `build/${platform}/client/release/src/net-connection-test`),
    ['--serve', profile], line => line === 'YOG test server ready')).child;
  const started = await start(path.join(root, `build/${platform}/gateway/release/glob2-ws-gateway`),
    ['--port', '0', '--origin', new URL(baseURL).origin], line => line.startsWith('gateway listening on '));
  gateway = started.child;
  endpoint = 'ws://127.0.0.1:' + started.line.split(':').pop().trim();
});
test.afterAll(async () => {
  await stop(gateway); await stop(lobby);
  if (work) await rm(work, {recursive: true, force: true});
  if (profile) await rm(path.join(os.homedir(), '.' + profile), {recursive: true, force: true});
});

test('browser YOG login exchanges the native protocol through the real gateway', async ({page}) => {
  await page.addInitScript(base => { globalThis.glob2Config = {websocketBase: base}; }, endpoint);
  const received = [], sent = [], errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  // Observe actual wire bytes; player actions below use only the real controls.
  page.on('websocket', socket => {
    socket.on('framesent', ({payload}) => sent.push(Buffer.from(payload)));
    socket.on('framereceived', ({payload}) => received.push(Buffer.from(payload)));
  });
  const types = chunks => {
    const bytes = Buffer.concat(chunks), result = [];
    for (let offset = 0; offset + 2 <= bytes.length;) {
      const length = bytes.readUInt16BE(offset);
      if (length === 0 || offset + 2 + length > bytes.length) break;
      result.push(bytes[offset + 2]); offset += length + 2;
    }
    return result;
  };
  const screen = name => expect.poll(async () => (await page.evaluate(() => glob2Diagnostics.snapshot())).screen).toContain(name);
  const click = (x, y) => page.locator('#canvas').click({position: {x, y}, delay: 80});
  await page.goto('/'); await screen('MainMenuScreen');
  await click(440, 490); await screen('YOGLoginScreen');
  await click(420, 510);
  await page.locator('#canvas').press('Home');
  for (let i = 0; i < 32; ++i) await page.locator('#canvas').press('Delete');
  await page.keyboard.type('transportfixture');
  await click(810, 590);
  // Server information, then refusal of an unregistered test account. This
  // proves bidirectional native/Wasm codecs without publishing or using accounts.
  await expect.poll(() => types(received)).toContain(10);
  await expect.poll(() => types(sent)).toContain(9);
  await expect.poll(() => types(sent)).toContain(1);
  await expect.poll(() => types(received)).toContain(7);
  await click(810, 650); await screen('MainMenuScreen');
  expect(errors).toEqual([]);
});

test('registered browser player enters and leaves the native YOG lobby', async ({page}, testInfo) => {
  await page.addInitScript(base => { globalThis.glob2Config = {websocketBase: base}; }, endpoint);
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  const screen = name => expect.poll(async () => (await page.evaluate(() => glob2Diagnostics.snapshot())).screen).toContain(name);
  const click = (x, y) => page.locator('#canvas').click({position: {x, y}, delay: 80});
  await page.goto('/'); await screen('MainMenuScreen');
  await click(440, 490); await screen('YOGLoginScreen');
  await click(420, 510);
  await page.locator('#canvas').press('Home');
  for (let i = 0; i < 32; ++i) await page.locator('#canvas').press('Delete');
  await page.keyboard.type('transportplayer');
  await click(420, 580);
  await page.locator('#canvas').press('Home');
  for (let i = 0; i < 32; ++i) await page.locator('#canvas').press('Delete');
  await page.keyboard.type('fixture-only');
  await click(810, 590);
  await screen('Glob2TabScreen');
  await page.screenshot({path: testInfo.outputPath('yog-lobby.png')});
  await page.locator('#canvas').press('Escape');
  await screen('MainMenuScreen');
  expect(errors).toEqual([]);
});
