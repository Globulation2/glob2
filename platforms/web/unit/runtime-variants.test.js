const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
function environment(variants) {
  const scripts = [], errors = [], redirects = [];
  const scope = {module:{exports:{}}, URL, URLSearchParams, Glob2BuildVariants:variants,
    location:{search:'', href:'https://example.test/play', replace:url=>redirects.push(url)},
    document:{createElement:()=>({}),body:{append:script=>scripts.push(script)}},
    Module:{renderer:'webgl2', onAbort:error=>errors.push(error)}};
  vm.runInNewContext(readFileSync(path.join(__dirname,'../loader.js'),'utf8'),scope);
  return {scope,scripts,errors,redirects,loader:scope.module.exports};
}
test('serial-only development package loads serial runtime without probing threads', async()=>{
  const env = environment(['serial']);
  await env.loader.load();
  assert.equal(env.scripts[0].src,'index.js');
  assert.equal(env.scope.Module.executionMode,'serial');
});
test('threaded-only package rejects unsupported host without fetching serial', async()=>{
  const env = environment(['threaded']);
  await assert.rejects(env.loader.load(),/cross-origin isolation unavailable/);
  assert.equal(env.scripts.length,0);
});
test('threaded-only package rejects a forced serial runtime', async()=>{
  const env = environment(['threaded']);
  env.scope.location.search='?threads=serial';
  await assert.rejects(env.loader.load(),/only the threaded runtime/);
});
test('normal package still falls back to serial on unsupported host', async()=>{
  const env = environment(['serial','threaded']);
  await env.loader.load();
  assert.equal(env.scripts[0].src,'index.js');
});
