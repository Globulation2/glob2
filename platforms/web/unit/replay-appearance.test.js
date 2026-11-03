const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {webcrypto, createHash} = require('node:crypto');
const shell = fs.readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const source = shell.slice(shell.indexOf('const WATCH_REPLAY_MAX_BYTES'), shell.indexOf('function chooseRenderer()'));
const match = '44444444-4444-4444-8444-444444444444';
const origin = 'https://play.example';
const route = `/api/v1/matches/${match}/artifacts/replay`;
const recording = Buffer.from('opaque replay bytes');
async function download(replay, responseUrl, crypto = webcrypto) {
  const context = vm.createContext({URL, URLSearchParams, Uint8Array, crypto,
    location:{origin, href:origin+'/play/', search:'?replay='+encodeURIComponent(replay)},
    fetch:async url => ({ok:true, url:responseUrl || url,
      headers:{get:()=>null}, arrayBuffer:async()=>recording})});
  vm.runInContext(source+'\nglobalThis.result=watchReplay;', context);
  await context.result.bytes;
  return JSON.parse(JSON.stringify(context.result));
}
test('same-origin match replay receives hash-bound companion metadata', async () => {
  const state = await download(route);
  assert.deepEqual(state.appearance,{format:1,origin,matchId:match,
    replaySha256:createHash('sha256').update(recording).digest('hex')});
});
test('external files, redirects and malformed match routes carry no instance authority', async () => {
  for (const [url,redirect] of [
    ['https://external.example'+route], [route,'https://external.example/file.replay'],
    [route,origin+'/other.replay'], ['/uploads/file.replay'],
    ['/api/v1/matches/invalid/artifacts/replay'], [route+'/extra'],
  ]) assert.equal((await download(url,redirect)).appearance,undefined);
});
test('unavailable digest support never prevents replay download', async () => {
  const state = await download(route,undefined,{});
  assert.equal(state.appearance,undefined);
  assert.equal(state.path,'/tmp/watch.replay');
});
