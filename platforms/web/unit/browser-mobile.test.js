const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
// Loads canvas-size.js's library functions (pthread branches kept) under a fake navigator.
function mobile(navigator) {
  const library = {};
  const source = readFileSync(path.join(__dirname, '../canvas-size.js'), 'utf8')
    .split('\n').filter(line => !line.startsWith('#')).join('\n');
  vm.runInContext(source, vm.createContext({navigator, addToLibrary:value => Object.assign(library, value)}));
  assert.equal(library.glob2_browser_mobile__proxy, 'sync');
  return library.glob2_browser_mobile();
}
const agents = {
  iphone: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1',
  android: 'Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Mobile Safari/537.36',
  androidTablet: 'Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36',
  mac: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15',
  windows: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36',
};
test('phones and tablets are mobile, including iPadOS behind a Mac user agent', () => {
  assert.equal(mobile({userAgent:agents.iphone, maxTouchPoints:5}), 1);
  assert.equal(mobile({userAgent:agents.android, userAgentData:{mobile:true}}), 1);
  assert.equal(mobile({userAgent:agents.androidTablet, userAgentData:{mobile:false}}), 1);
  assert.equal(mobile({userAgent:agents.mac, maxTouchPoints:5}), 1);
});
test('desktops are not mobile, touch screens included', () => {
  assert.equal(mobile({userAgent:agents.mac, maxTouchPoints:0}), 0);
  assert.equal(mobile({userAgent:agents.windows, maxTouchPoints:10, userAgentData:{mobile:false}}), 0);
  assert.equal(mobile(undefined), 0);
});
