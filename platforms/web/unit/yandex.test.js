const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../yandex');
const {fakeWindow} = require('./portal-fixture');

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
  await adapter.connect();
  adapter.session.visible(false);
  assert.deepEqual(calls, ['ready', 'start', 'stop']);
});

test('game_api_pause stops gameplay and audio until game_api_resume', async () => {
  const {calls, handlers, win, adapter} = fixture({lang: 'en'});
  const context = new win.AudioContext();
  await adapter.connect();
  adapter.session.start();
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
