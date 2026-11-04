const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');

test('the production text bridge retains Thai IME composition across game frames', () => {
  let input;
  const handlers = {};
  const context = vm.createContext({
    Module: {glob2PresentationSurface: {}}, TextEncoder,
    browserText: source => source === 'Game text field' ? 'ช่องข้อความในเกม' : source,
    canvas: {getBoundingClientRect: () => ({left: 0, top: 0, width: 800, height: 600})},
    document: {
      createElement: () => (input = {
        style: {}, dataset: {}, attributes: {}, value: '', selectionEnd: 0,
        setAttribute(name, value) { this.attributes[name] = value; },
        addEventListener(name, handler) { handlers[name] = handler; },
      }),
      body: {appendChild() {}},
    },
  });
  const start = shell.indexOf('Module.textBridge = (() => {');
  const end = shell.indexOf('\n})();', start) + '\n})();'.length;
  assert.ok(start >= 0 && end > start);
  vm.runInContext(shell.slice(start, end), context);
  const bridge = context.Module.textBridge;
  const rect = {x: 10, y: 10, w: 300, h: 40};
  bridge.field(1, rect, 800, 600, '', false, 100, rect, false);
  assert.equal(input.attributes['aria-label'], 'ช่องข้อความในเกม');
  assert.match(input.style.cssText, /var\(--glob2-script-body/);
  handlers.compositionstart();
  input.value = 'ผู้เล่น';
  handlers.input();
  assert.equal(bridge.take(1), null);
  // A subsequent native frame still has the previous committed value.
  bridge.field(1, rect, 800, 600, '', false, 100, rect, false);
  assert.equal(input.value, 'ผู้เล่น');
  input.value = 'ผู้เล่นกำลังก่อสร้าง';
  input.selectionEnd = input.value.length;
  handlers.compositionend();
  const committed = bridge.take(1);
  assert.equal(committed.value, 'ผู้เล่นกำลังก่อสร้าง');
  assert.equal(committed.cursor, input.value.length);
  assert.equal(bridge.take(1), null);
});
