const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf('function browserMapLaunchArguments(');
const end = shell.indexOf('// End browser map launch arguments.', start);
const context = vm.createContext({URLSearchParams});
vm.runInContext(shell.slice(start, end), context);
const launch = (search) => Array.from(context.browserMapLaunchArguments(search, 'https://example.org'));
const id = '5f6a7b8c-9d0e-4f1a-8b2c-3d4e5f6a7b8c', hash = 'a'.repeat(64);
const query = `?map=${id}&version=${hash}&title=Two+%26+Three`;
test('browser launches the selected mode with exact map and serving origin', () => {
  for (const mode of ['local', 'multiplayer'])
    assert.deepEqual(launch(`${query}&mode=${mode}`), [mode === 'local' ? '--local-map' : '--room-map', id, hash, 'Two & Three', '--instance', 'https://example.org']);
  assert.equal(launch(query)[0], '--room-map');
});
test('invalid launches cannot select a different destination or malformed map', () => {
  for (const bad of ['?map=x&version=' + hash, `?map=${id}&version=x`, query + '&mode=unknown'])
    assert.deepEqual(launch(bad), []);
});
