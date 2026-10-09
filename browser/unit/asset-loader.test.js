const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const Loader = require('../asset-loader');

const text = value => new TextEncoder().encode(value);
function manifest() {
  return {version:1, packages:[
    {name:'core', optional:false, size:9, parts:[{url:'assets/core.aaaaaaaaaaaaaaaa.data', size:9,
      files:[['/data/a.txt', 0, 4], ['/maps/b.map', 4, 9]]}]},
    {name:'hd', optional:true, size:6, parts:[
      {url:'assets/hd-1.bbbbbbbbbbbbbbbb.data', size:3, files:[['/data/highres/v1/x.png', 0, 3]]},
      {url:'assets/hd-2.cccccccccccccccc.data', size:3, files:[['/data/highres/v1/frames.txt', 0, 3]]}]},
  ]};
}
const bodies = {
  'assets/core.aaaaaaaaaaaaaaaa.data': ['abcd', 'efghi'],
  'assets/hd-1.bbbbbbbbbbbbbbbb.data': ['png'],
  'assets/hd-2.cccccccccccccccc.data': ['txt'],
};
// Responses stream their body in the given chunks, like a decoded HTTP body.
function response(chunks, status = 200) {
  const queue = chunks.map(text);
  return {ok:status === 200, status, body:{getReader:() => ({read:async () => queue.length ? {done:false, value:queue.shift()} : {done:true}})}};
}
class MemoryCache {
  constructor() { this.entries = new Map(); }
  async match(url) { return this.entries.has(url) ? {arrayBuffer:async () => this.entries.get(url).buffer} : undefined; }
  async put(url, value) { this.entries.set(url, value.bytes.slice()); }
  async delete(request) { return this.entries.delete(request.url ?? request); }
  async keys() { return [...this.entries.keys()].map(url => ({url})); }
}
function host(cache = new MemoryCache()) {
  const requests = [], files = new Map(), directories = [], progress = [];
  return {
    requests, files, directories, progress, cache,
    caches:{open:async () => cache},
    Response:class { constructor(bytes) { this.bytes = bytes; } },
    resolve:url => 'https://example.test/play/' + url,
    fetch:async url => {
      requests.push(url);
      const name = url.replace('https://example.test/play/', '');
      return bodies[name] ? response(bodies[name]) : response([], 404);
    },
    fs:{
      analyzePath:path => ({exists:files.has(path)}),
      unlink:path => { assert.ok(files.delete(path), 'unlink of a missing file ' + path); },
      createPath:(parent, directory) => directories.push(directory),
      createDataFile:(directory, name, data, read, write, own) => {
        assert.equal(own, true);
        assert.ok(!files.has(directory + '/' + name), 'the file system refuses to overwrite ' + name);
        files.set(directory + '/' + name, new TextDecoder().decode(data));
      },
    },
    onProgress:(name, loaded, total) => progress.push([name, loaded, total]),
  };
}

test('installs a package into the file system with byte progress', async () => {
  const environment = host();
  const loader = new Loader(manifest(), environment);
  assert.deepEqual(loader.state(), {core:'pending', hd:'idle'});
  await loader.load('core');
  assert.deepEqual(Object.fromEntries(environment.files), {'/data/a.txt':'abcd', '/maps/b.map':'efghi'});
  assert.deepEqual(environment.progress, [['core', 0, 9], ['core', 4, 9], ['core', 9, 9]]);
  assert.equal(loader.state().core, 'ready');
});

test('a later package replaces a core file and is reported once to the game', async () => {
  const replacing = manifest();
  replacing.packages.push({name:'font-cjk', optional:true, size:3, parts:[
    {url:'assets/font-cjk.eeeeeeeeeeeeeeee.data', size:3, files:[['/data/a.txt', 0, 3]]}]});
  bodies['assets/font-cjk.eeeeeeeeeeeeeeee.data'] = ['xyz'];
  const environment = host();
  const loader = new Loader(replacing, environment);
  await loader.load('core');
  assert.deepEqual(loader.takeInstalled(), ['core']);
  await loader.load('font-cjk');
  assert.equal(environment.files.get('/data/a.txt'), 'xyz');
  assert.deepEqual(loader.takeInstalled(), ['font-cjk']);
  assert.deepEqual(loader.takeInstalled(), []);
  await loader.load('font-cjk');
  assert.deepEqual(loader.takeInstalled(), [], 'a ready package is not installed twice');
});

test('a later visit installs cached parts without the network', async () => {
  const cache = new MemoryCache();
  await new Loader(manifest(), host(cache)).load('core');
  const second = host(cache);
  await new Loader(manifest(), second).load('core');
  assert.deepEqual(second.requests, []);
  assert.equal(second.files.get('/maps/b.map'), 'efghi');
});

test('optional parts wait for the page between downloads and appear only when complete', async () => {
  const environment = host();
  const loader = new Loader(manifest(), environment);
  let release;
  let waits = 0;
  const pending = loader.load('hd', () => { waits++; return waits === 2 ? new Promise(resolve => { release = resolve; }) : null; });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(environment.requests.length, 1);
  assert.equal(environment.files.size, 0, 'no file is visible before the last part');
  release();
  await pending;
  assert.deepEqual([...environment.files.keys()], ['/data/highres/v1/x.png', '/data/highres/v1/frames.txt']);
  assert.equal(loader.state().hd, 'ready');
});

test('failed downloads are reported and leave nothing behind', async () => {
  const broken = manifest();
  broken.packages[0].parts[0].url = 'assets/missing.dddddddddddddddd.data';
  const environment = host();
  const loader = new Loader(broken, environment);
  await assert.rejects(loader.load('core'), /HTTP 404/);
  assert.equal(loader.state().core, 'failed');
  assert.equal(environment.files.size, 0);
});

test('a response of the wrong size is rejected', async () => {
  const environment = host();
  environment.fetch = async () => response(['abc']);
  await assert.rejects(new Loader(manifest(), environment).load('core'), /Incomplete download/);
});

test('works without Cache Storage and prunes parts of older builds', async () => {
  const environment = host();
  environment.caches = null;
  await new Loader(manifest(), environment).load('core');
  assert.equal(environment.files.size, 2);
  const cache = new MemoryCache();
  cache.entries.set('https://example.test/play/assets/core.0000000000000000.data', text('old'));
  const loader = new Loader(manifest(), host(cache));
  await loader.load('core');
  await loader.prune();
  assert.deepEqual([...cache.entries.keys()], ['https://example.test/play/assets/core.aaaaaaaaaaaaaaaa.data']);
});

test('progress follows transfer bytes when a part arrives compressed', async () => {
  const environment = host();
  environment.wireSize = (url, encoding) => url.startsWith('assets/core.') && encoding === 'br' ? 3 : null;
  const plain = environment.fetch;
  environment.fetch = async url => Object.assign(await plain(url), {headers:{get:name => name === 'Content-Encoding' ? 'br' : null}});
  await new Loader(manifest(), environment).load('core');
  assert.deepEqual(environment.progress.map(([, loaded, total]) => [Number(loaded.toFixed(3)), total]),
    [[0, 3], [1.333, 3], [3, 3]]);
  assert.equal(environment.files.get('/maps/b.map'), 'efghi');
});

test('the loading page estimates remaining time from the rate so far', () => {
  const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
  const start = shell.indexOf('function loadingEstimate(');
  const end = shell.indexOf('// End loading estimate.', start);
  const context = vm.createContext({browserText:(source, params = {}) => source.replace(/\{(\w+)\}/g, (match, key) => String(params[key] ?? match))});
  vm.runInContext(shell.slice(start, end), context);
  const estimate = (...args) => ({...context.loadingEstimate(...args)});
  assert.deepEqual(estimate(0, 30e6, 0), {percent:0, text:'0.0 of 30 MB'});
  assert.deepEqual(estimate(3e6, 30e6, 1000), {percent:10, text:'3.0 of 30 MB'});
  assert.deepEqual(estimate(3e6, 30e6, 10000), {percent:10, text:'3.0 of 30 MB · about 2 min left'});
  assert.deepEqual(estimate(15e6, 30e6, 30000), {percent:50, text:'15 of 30 MB · about 30 s left'});
  assert.deepEqual(estimate(29e6, 30e6, 29000), {percent:96, text:'29 of 30 MB · a few seconds left'});
  assert.deepEqual(estimate(30e6, 30e6, 30000), {percent:100, text:'30 of 30 MB'});
});


test('on-demand requests share a load and throttle failed retries', async () => {
  const environment = host();
  const loader = new Loader(manifest(), environment);
  let release;
  let calls = 0;
  environment.fetch = () => { calls++; return new Promise(resolve => { release = resolve; }); };
  loader.request('core', 100);
  loader.request('core', 200);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(calls, 1);
  assert.equal(loader.states.core, 'downloading');
  release(response([], 503));
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(loader.states.core, 'failed');
  loader.request('core', 10099);
  assert.equal(calls, 1);
  environment.fetch = async () => { calls++; return response(bodies['assets/core.aaaaaaaaaaaaaaaa.data']); };
  loader.request('core', 10100);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(calls, 2);
  assert.equal(loader.states.core, 'ready');
  loader.request('core', 99999);
  assert.equal(calls, 2);
  assert.equal(environment.files.get('/maps/b.map'), 'efghi');
});

// Exercise the actual Emscripten pre-js integration, with a delayed sprite
// transfer. Core finishing must not release startup before game is installed.
test('startup downloads core and game together and waits for both installations', async () => {
  const packages = manifest();
  packages.packages.push({name:'game', optional:true, size:3, parts:[
    {url:'assets/game.ffffffffffffffff.data', size:3, files:[['/data/unit.txt', 0, 3]]}]});
  const environment = host();
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  const dependencies = new Set();
  const context = vm.createContext({
    Module:{glob2AssetManifest:packages}, window:{}, document:{baseURI:'https://example.test/play/'},
    URL, Uint8Array, Response:environment.Response, caches:environment.caches, FS:environment.fs,
    fetch:async url => {
      if (url.includes('/game.')) {
        environment.requests.push(url);
        await gate;
        return response(['xyz']);
      }
      return environment.fetch(url);
    },
    addRunDependency:name => dependencies.add(name),
    removeRunDependency:name => dependencies.delete(name),
  });
  vm.runInContext(readFileSync(path.join(__dirname, '../asset-loader.js'), 'utf8'), context);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(environment.requests.length, 2, 'both downloads start before preRun');
  context.Module.preRun.forEach(callback => callback());
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(context.Module.glob2Assets.states.core, 'ready');
  assert.equal(context.Module.glob2Assets.states.game, 'downloading');
  assert.deepEqual([...dependencies], ['glob2-assets-game']);
  assert.equal(environment.files.has('/data/unit.txt'), false);
  release();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(context.Module.glob2Assets.states.game, 'ready');
  assert.equal(dependencies.size, 0);
  assert.equal(environment.files.get('/data/unit.txt'), 'xyz');
  assert.equal(environment.requests.length, 2, 'startup does not fetch either package twice');
});

test('part concurrency is bounded globally across packages and duplicate loads share work', async () => {
  const environment = host();
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  let active = 0, peak = 0, started = 0;
  environment.fetch = async () => {
    peak = Math.max(peak, ++active); started++;
    await gate; active--;
    return response(['x']);
  };
  const packages = ['one', 'two'].map(name => ({name, optional:true,
    parts:Array.from({length:5}, (_, i) => ({url:name + i, size:1, files:[['/' + name + i, 0, 1]]}))}));
  const loader = new Loader({packages}, environment);
  const first = loader.load('one');
  assert.equal(loader.load('one'), first);
  const second = loader.load('two');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(started, 4);
  assert.equal(environment.files.size, 0);
  release(); await Promise.all([first, second]);
  assert.equal(started, 10); assert.equal(peak, 4);
  assert.equal(environment.files.size, 10);
});

test('out-of-order downloads preserve package replacement order', async () => {
  const environment = host();
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  environment.fetch = async url => {
    if (url.endsWith('first')) { await gate; return response(['old']); }
    return response(['new']);
  };
  const packages = ['first', 'second'].map(name => ({name, optional:true,
    parts:[{url:name, size:3, files:[['/data/shared', 0, 3]]}]}));
  const loader = new Loader({packages}, environment);
  const first = loader.load('first'), second = loader.load('second');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(environment.files.size, 0);
  release(); await Promise.all([first, second]);
  assert.equal(environment.files.get('/data/shared'), 'new');
  assert.deepEqual(loader.takeInstalled(), ['first', 'second']);
});
