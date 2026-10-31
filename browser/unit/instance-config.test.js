const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf('function browserInstanceConfig(');
const end = shell.indexOf('// End browser instance configuration.', start);
const context = vm.createContext({});
vm.runInContext(shell.slice(start, end), context);
const update = context.browserInstanceConfig;

test('fresh browser profiles use the game hosting origin', () => {
  assert.deepEqual(JSON.parse(update(null, 'https://app.glob2.online')), {
    version:1, selected:'https://app.glob2.online', instances:{}
  });
  assert.equal(JSON.parse(update(null, 'https://custom.example')).selected, 'https://custom.example');
});
test('official cutover preserves credentials under their original origin', () => {
  for (const former of ['https://glob2online.com', 'https://app.glob2online.com']) {
    const instances = {[former]:{refreshToken:'old-origin-only'}};
    const config = JSON.parse(update(JSON.stringify({version:1,selected:former,instances}), 'https://app.glob2.online'));
    assert.equal(config.selected, 'https://app.glob2.online');
    assert.deepEqual(config.instances, instances);
    assert.equal(config.instances['https://app.glob2.online'], undefined);
  }
});
test('custom selections and corrupt or future-version storage are left intact', () => {
  for (const value of ['broken', JSON.stringify({version:2,selected:'https://glob2online.com',instances:{}}), JSON.stringify({version:1,selected:'https://custom.example',instances:{}})]) {
    assert.equal(update(value, 'https://app.glob2.online'), null);
  }
  assert.equal(update(JSON.stringify({version:1,selected:'https://glob2online.com',instances:{}}), 'https://custom.example'), null);
});
