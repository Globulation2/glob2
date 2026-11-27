const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../yandex');
const {fakeWindow, adWindow, settle} = require('./portal-fixture');

function fixture({lang = 'ru', locale = 'en', init} = {}) {
  const calls = [];
  const handlers = {};
  const ysdk = {
    environment: {i18n: {lang}},
    on: (name, handler) => { handlers[name] = handler; },
    features: {
      LoadingAPI: {ready: () => calls.push('ready')},
      GameplayAPI: {start: () => calls.push('start'), stop: () => calls.push('stop')},
    },
  };
  const i18n = {
    current: locale,
    initialize: () => Promise.resolve(),
    getLocale: () => i18n.current,
    resolveLocale: tags => ({ru: 'ru', tr: 'tr', en: 'en'})[tags[0]] || 'en',
    setLocale: code => { calls.push('setLocale ' + code); i18n.current = code; return Promise.resolve(); },
  };
  const win = fakeWindow({YaGames: {init: init || (() => Promise.resolve(ysdk))}, Glob2I18n: i18n});
  return {win, calls, handlers, adapter: create(win)};
}

test('the Yandex language becomes the interface locale', async () => {
  const {calls, win, adapter} = fixture({lang: 'ru'});
  await adapter.connect();
  assert.equal(win.Glob2I18n.getLocale(), 'ru');
  assert.deepEqual(calls, ['setLocale ru']);
});

test('a matching locale is left alone', async () => {
  const {calls, adapter} = fixture({lang: 'en'});
  await adapter.connect();
  assert.deepEqual(calls, []);
});

test('LoadingAPI.ready and gameplay follow the ready menu, also when it came first', async () => {
  const {calls, adapter} = fixture({lang: 'en'});
  adapter.session.start();
  adapter.session.match(true);
  await adapter.connect();
  adapter.session.visible(false);
  assert.deepEqual(calls, ['ready', 'start', 'stop']);
});

test('game_api_pause stops gameplay and audio until game_api_resume', async () => {
  const {calls, handlers, win, adapter} = fixture({lang: 'en'});
  const context = new win.AudioContext();
  await adapter.connect();
  adapter.session.start();
  adapter.session.match(true);
  handlers.game_api_pause();
  assert.equal(context.state, 'suspended');
  handlers.game_api_resume();
  assert.equal(context.state, 'running');
  assert.deepEqual(calls, ['ready', 'start', 'stop', 'start']);
});

test('a failed init leaves the game running without the SDK', async () => {
  const {calls, adapter} = fixture({init: () => Promise.reject(new Error('offline'))});
  assert.equal(await adapter.connect(), null);
  adapter.session.start();
  assert.deepEqual(calls, []);
});

function adFixture() {
  const calls = [];
  const options = {};
  const ysdk = {
    environment: {i18n: {lang: 'en'}},
    on() {},
    features: {LoadingAPI: {ready() {}},
               GameplayAPI: {start: () => calls.push('start'), stop: () => calls.push('stop')}},
    adv: {showFullscreenAdv(value) { calls.push('showFullscreenAdv'); Object.assign(options, value); }},
  };
  const win = adWindow('yandex', {YaGames: {init: () => Promise.resolve(ysdk)}});
  return {win, calls, options, adapter: create(win)};
}

test('a finished match shows one fullscreen ad and gameplay resumes after onClose', async () => {
  const {win, calls, options, adapter} = adFixture();
  const context = new win.AudioContext();
  await adapter.connect();
  adapter.session.start();
  assert.equal(win.glob2PortalHost.matchBreak('start'), false);
  win.glob2PortalHost.matchBreak('end');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), true);
  options.callbacks.onOpen();
  assert.equal(context.state, 'suspended');
  options.callbacks.onClose(true); await settle();
  assert.equal(win.glob2PortalHost.held, false);
  assert.equal(context.state, 'running');
  assert.equal(win.glob2PortalHost.matchBreak('start'), false);
  win.glob2PortalHost.matchBreak('end');
  win.timers.advance(180000);
  assert.equal(win.glob2PortalHost.matchBreak('leave'), true);
  options.callbacks.onClose(false); await settle();
  assert.deepEqual(calls, ['start', 'stop', 'showFullscreenAdv', 'start', 'stop', 'showFullscreenAdv']);
});

test('onError ends a Yandex break', async () => {
  const {win, options, adapter} = adFixture();
  await adapter.connect();
  win.glob2PortalHost.matchBreak('start');
  win.glob2PortalHost.matchBreak('leave');
  options.callbacks.onError(new Error('no fill')); await settle();
  assert.equal(win.glob2PortalHost.held, false);
});

test('without the Yandex SDK a break holds nothing', async () => {
  const win = adWindow('yandex');
  create(win);
  win.glob2PortalHost.matchBreak('start');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), false);
});

test('GameplayAPI stops for the in-game menu and pauses and starts on resume, with no ad', async () => {
  const {win, calls, adapter} = adFixture();
  await adapter.connect();
  adapter.session.start();
  for (const point of ['start', 'pause', 'resume', 'pause', 'end'])
    win.glob2PortalHost.matchBreak(point);
  assert.deepEqual(calls, ['start', 'stop', 'start', 'stop']);
  assert.equal(win.glob2PortalHost.matchBreak('leave'), true);
  assert.deepEqual(calls.slice(-1), ['showFullscreenAdv']);
});

test('the Yandex page watches window focus: blur stops gameplay and audio until focus returns', async () => {
  const {calls, win, adapter} = fixture({lang: 'en'});
  const listeners = {};
  win.addEventListener = (name, listener) => { listeners[name] = listener; };
  win.document = {hidden: false, body: null, hasFocus: () => true, addEventListener() {}};
  const context = new win.AudioContext();
  await adapter.connect();
  require('../portal-core').watchShell(win, adapter.session, {focus: true, audio: adapter.audio});
  adapter.session.start();
  adapter.session.match(true);
  listeners.blur();
  assert.equal(context.state, 'suspended');
  listeners.focus();
  assert.equal(context.state, 'running');
  assert.deepEqual(calls, ['ready', 'start', 'stop', 'start']);
});

test('the Yandex bootstrap asks watchShell for focus handling', () => {
  const source = require('node:fs').readFileSync(require('node:path').join(__dirname, '../yandex.js'), 'utf8');
  assert.match(source, /watchShell\(window, adapter\.session, \{focus: true/);
});
