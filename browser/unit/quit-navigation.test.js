const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf('  onGameExit(code) {');
const end = shell.indexOf('  onAbort(reason)', start);
assert.ok(start >= 0 && end > start);

function quit(pathname, code) {
  const navigation = [], messages = [];
  const context = vm.createContext({
    location: {pathname, assign: target => navigation.push(target)},
    Module: {print: message => messages.push(message)},
  });
  vm.runInContext('const hooks = {' + shell.slice(start, end) + '}; hooks.onGameExit(' + code + ');', context);
  return {navigation, messages};
}

test('successful hosted quit returns the current tab to the online home page', () => {
  for (const pathname of ['/play', '/play/', '/play/index.html']) {
    assert.deepEqual(quit(pathname, 0), {navigation: ['/'], messages: []});
  }
});

test('abnormal exits and standalone hosts retain their exit message', () => {
  for (const [pathname, code] of [['/play/', 1], ['/', 0], ['/index.html', 0], ['/playground/', 0]]) {
    const result = quit(pathname, code);
    assert.deepEqual(result.navigation, []);
    assert.deepEqual(result.messages, [`Game exited with code ${code}. Reload the page to restart.`]);
  }
});
