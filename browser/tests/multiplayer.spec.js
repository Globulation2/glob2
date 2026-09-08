const {gameURL} = require('./game-url');
const {test, expect} = require('@playwright/test');
// Continuous trace screenshots force readback from both WebGL contexts on each
// input action. Keep diagnostic traces and capture gameplay explicitly below.
test.use({trace:{mode:'retain-on-failure', screenshots:false, snapshots:true, sources:true}});
const {spawn} = require('node:child_process');
const {createInterface} = require('node:readline');
const {mkdtemp, rm, readFile} = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {randomUUID} = require('node:crypto');

const root = path.resolve(__dirname, '../..');
const platform = os.platform() === 'win32' ? 'windows' : os.platform();
let lobby, gateway, tlsForwarder, work, profile, endpoint, secureEndpoint;
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
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
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
  const port = started.line.split(':').pop().trim();
  endpoint = 'ws://127.0.0.1:' + port;
  const tls = await start('python3', [path.join(root, 'tests/transport/tls_forwarder.py'), work, port],
    line => line.startsWith('TLS forwarder listening on '));
  tlsForwarder = tls.child;
  secureEndpoint = 'wss://localhost:' + tls.line.split(' ').pop();
});
test.afterAll(async () => {
  await stop(tlsForwarder); await stop(gateway); await stop(lobby);
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
  await page.goto(gameURL()); await screen('MainMenuScreen');
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
  await page.goto(gameURL()); await screen('MainMenuScreen');
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

async function loginPlayer(page, name) {
  await page.addInitScript(base => { globalThis.glob2Config = {websocketBase: base}; }, endpoint);
  const screen = target => expect.poll(async () => (await page.evaluate(() => glob2Diagnostics.snapshot())).screen).toContain(target);
  const click = (x, y) => page.locator('#canvas').click({position: {x, y}, delay: 80});
  await page.goto(gameURL()); await screen('MainMenuScreen');
  await click(440, 490); await screen('YOGLoginScreen');
  for (const [y, value] of [[510, name], [580, 'fixture-only']]) {
    await click(420, y); await page.locator('#canvas').press('Home');
    for (let i = 0; i < 32; ++i) await page.locator('#canvas').press('Delete');
    await page.keyboard.type(value);
  }
  await click(810, 590); await screen('Glob2TabScreen');
}
function receivedTypes(page, direction = 'framereceived') {
  const result = [];
  page.on('websocket', socket => {
    let bytes = Buffer.alloc(0);
    socket.on(direction, ({payload}) => {
      bytes = Buffer.concat([bytes, Buffer.from(payload)]);
      while (bytes.length >= 2) {
        const size = bytes.readUInt16BE(0);
        if (!size || bytes.length < size + 2) break;
        result.push(bytes[2]); bytes = bytes.subarray(size + 2);
      }
    });
  });
  return () => result;
}

function sentChecksums(page) {
  const checksums = [];
  page.on('websocket', socket => {
    let bytes = Buffer.alloc(0);
    socket.on('framesent', ({payload}) => {
      bytes = Buffer.concat([bytes, Buffer.from(payload)]);
      while (bytes.length >= 2) {
        const size = bytes.readUInt16BE(0);
        if (!size || bytes.length < size + 2) break;
        const message = bytes.subarray(2, size + 2);
        if (message[0] === 44 && message.length >= 11) { // NetSendOrder
          const checksum = message.readUInt32BE(message.length - 4);
          if (checksum !== 0xffffffff) checksums.push(checksum);
        }
        bytes = bytes.subarray(size + 2);
      }
    });
  });
  return checksums;
}

const multiplayerAIs = ['no AI', 'Numbi', 'Castor', 'Warrush', 'ReachToInfinity', 'Nicowar', 'Cortex']
  .map((name, id) => ({name, id}))
  .filter(ai => process.env.GLOB2_ALL_AIS === '1' || ai.id === 0 || ai.id === 6);
for (const ai of multiplayerAIs)
test(`two browser players create, join and start a YOG match (${ai.name})`, async ({page, browser, baseURL}, testInfo) => {
  // Two WebGL clients share the headless browser's software GPU. This is a
  // correctness fixture; controlled performance gates use a reference GPU.
  test.setTimeout(180000);
  const other = await browser.newContext({baseURL, viewport: {width: 1200, height: 900}});
  const guest = await other.newPage();
  try {
    const hostTypes = receivedTypes(page), guestTypes = receivedTypes(guest);
    const guestSent = receivedTypes(guest, 'framesent'), hostSent = receivedTypes(page, 'framesent');
    const hostChecksums = sentChecksums(page), guestChecksums = sentChecksums(guest);
    const errors = [];
    for (const target of [page, guest]) target.on('pageerror', error => errors.push(String(error)));
    const click = (target, x, y) => target.locator('#canvas').click({position: {x, y}, delay: 80});
    await loginPlayer(page, 'transportplayer');
    await loginPlayer(guest, 'transportguest');
    const lists = guestTypes().filter(type => type === 50).length;
    await click(page, 1090, 815);
    await expect.poll(async () => (await page.evaluate(() => glob2Diagnostics.snapshot())).screen).toContain('ChooseMapScreen');
    await click(page, 380, 280); await click(page, 810, 590);
    await expect.poll(hostTypes).toContain(16); // NetCreateGameAccepted
    await expect.poll(() => guestTypes().filter(type => type === 50).length).toBeGreaterThan(lists);
    await click(guest, 100, 130); await click(guest, 1090, 245);
    await expect.poll(guestTypes).toContain(18); // NetGameJoinAccepted
    await expect.poll(guestSent).toContain(47); // NetSetGameInRouter
    await guest.screenshot({path: testInfo.outputPath('joined-room.png')});
    if (ai.id) {
      await click(page, 1090, 410 - 30 * (ai.id - 1));
      await expect.poll(hostSent).toContain(12); // NetAddAI
    }
    const ready = hostTypes().filter(type => type === 26).length;
    await click(guest, 1170, 455);
    await expect.poll(guestSent).toContain(26);
    await expect.poll(() => hostTypes().filter(type => type === 26).length).toBeGreaterThan(ready);
    await click(page, 1090, 475);
    for (const target of [page, guest])
      await expect.poll(async () => (await target.evaluate(() => glob2Diagnostics.snapshot())).tick, {timeout:60000}).toBeGreaterThan(100);
    await expect.poll(() => Math.min(hostChecksums.length, guestChecksums.length)).toBeGreaterThanOrEqual(25);
    const count = Math.min(hostChecksums.length, guestChecksums.length);
    expect(count).toBeGreaterThanOrEqual(25);
    expect(hostChecksums.slice(0, count)).toEqual(guestChecksums.slice(0, count));
    expect(errors).toEqual([]);
    await testInfo.attach('matching-order-checksums', {body: JSON.stringify({count, checksums: hostChecksums.slice(0, count)}), contentType: 'application/json'});
    await page.screenshot({path: testInfo.outputPath('multiplayer-match.png')});
  } finally { await other.close(); }
});


for (const transport of ['TCP', 'WSS'])
test(`browser and native players complete matching simulation checkpoints (${transport})`, async ({page}, testInfo) => {
  const nativeProfile = 'glob2-native-peer-' + randomUUID();
  let peer;
  const peerLog = [];
  try {
    const hostTypes = receivedTypes(page), checksums = sentChecksums(page);
    const click = (x, y) => page.locator('#canvas').click({position: {x, y}, delay: 80});
    await loginPlayer(page, 'transportplayer');
    await click(1090, 815);
    await expect.poll(async () => (await page.evaluate(() => glob2Diagnostics.snapshot())).screen).toContain('ChooseMapScreen');
    await click(380, 280); await click(810, 590);
    await expect.poll(hostTypes).toContain(16);
    const started = await start(path.join(root, `build/${platform}/client/release/src/native-multiplayer-peer`),
      transport === 'WSS' ? [nativeProfile, secureEndpoint, path.join(work, 'cert.pem')] : [nativeProfile],
      line => line.startsWith('native peer joined order-rate='));
    peer = started.child;
    peer.stdout.on('data', chunk => peerLog.push(String(chunk)));
    peer.stderr.on('data', chunk => peerLog.push(String(chunk)));
    await expect.poll(() => hostTypes().filter(type => type === 26).length).toBeGreaterThanOrEqual(2);
    await click(1090, 475);
    await expect.poll(async () => (await page.evaluate(() => glob2Diagnostics.snapshot())).tick).toBeGreaterThan(125);
    await page.screenshot({path: testInfo.outputPath('native-cross-play.png')});
    await expect.poll(() => peer.exitCode ?? peer.signalCode, {timeout: 45000}).toBe(0);
    const bytes = await readFile(path.join(os.homedir(), '.' + nativeProfile, 'replays/last_game.replay.checksums'));
    const teams = bytes.readUInt32LE(4), count = bytes.readUInt32LE(12);
    expect(count).toBeGreaterThanOrEqual(250);
    const native = new Map();
    let offset = 20;
    const u32 = () => { const value = bytes.readUInt32LE(offset); offset += 4; return value; };
    for (let i = 0; i < count; ++i) {
      const tick = u32(), checksum = u32(); native.set(tick, checksum);
      for (let team = 0; team < teams; ++team) {
        u32(); // Team checksum.
        for (let group = 0; group < 2; ++group) {
          const objects = u32();
          for (let object = 0; object < objects; ++object) {
            offset += 6; // Object ID and checksum.
            const fields = u32(); offset += fields * 4;
          }
        }
      }
    }
    expect(offset).toBe(bytes.length);
    expect(checksums.length).toBeGreaterThanOrEqual(25);
    await testInfo.attach('native-checkpoints', {body: bytes, contentType: 'application/octet-stream'});
    await testInfo.attach('browser-order-checksums', {body: JSON.stringify(checksums), contentType: 'application/json'});
    await testInfo.attach('checksum-alignment', {body: JSON.stringify({native: [...native].slice(0, 30), browser: checksums.slice(0, 8)}), contentType: 'application/json'});
    // YOG finalizes the order rate at match start, after room readiness.
    const orderRate = Number(/native match order-rate=(\d+)/.exec(peerLog.join(''))?.[1]);
    expect(orderRate).toBeGreaterThan(0);
    // Align command-boundary checksums using the negotiated order rate.
    const compared = Math.min(checksums.length, Math.ceil(count / orderRate));
    for (let i = 0; i < compared; ++i) expect(checksums[i]).toBe(native.get(i * orderRate));

  } finally {
    await stop(peer);
    await testInfo.attach('native-peer-log', {body: peerLog.join(''), contentType: 'text/plain'});
    await rm(path.join(os.homedir(), '.' + nativeProfile), {recursive: true, force: true});
  }
});
