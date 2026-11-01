const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf('Module.textBridge = (() => {');
const end = shell.indexOf('\n})();', start) + '\n})();'.length;
assert.ok(start >= 0 && end > start);

function profile({maximum = 32, value = '', segmenter = true} = {}) {
  const elements = [];
  function element() {
    const listeners = new Map();
    let text = '';
    return {
      style:{}, dataset:{}, selectionStart:0, selectionEnd:0,
      setAttribute() {}, remove() {},
      get value() { return text; },
      set value(next) { text = next; this.selectionStart = this.selectionEnd = text.length; },
      addEventListener(type, handler) {
        const handlers = listeners.get(type) || []; handlers.push(handler); listeners.set(type, handlers);
      },
      fire(type, options = {}) {
        const event = {stopPropagation() {}, preventDefault() {}, ...options};
        for (const handler of listeners.get(type) || []) handler(event);
      },
      focus() { this.fire('focus'); }, blur() {}, select() {},
    };
  }
  const context = vm.createContext({
    Module:{glob2PresentationSurface:{}}, TextEncoder,
    Intl:segmenter ? Intl : {}, browserText:value => value,
    canvas:{getBoundingClientRect:() => ({left:0,top:0,width:400,height:200})},
    document:{createElement:element,body:{appendChild:input => elements.push(input)}},
  });
  vm.runInContext(shell.slice(start, end), context);
  const bridge = context.Module.textBridge;
  const rect = {x:0,y:0,w:350,h:50};
  const draw = next => bridge.field(1, rect, 400, 200, next, false, maximum, rect, false);
  draw(value);
  const input = elements[0];
  return {bridge, input, draw, edit(next) { input.value = next; input.fire('input'); return input.value; }};
}

test('Bengali byte limit preserves the complete final conjunct', () => {
  const p = profile();
  const value = 'ক'.repeat(8) + 'ক্ষ';
  assert.equal(Buffer.byteLength(value), 33);
  assert.equal(p.edit(value), 'ক'.repeat(8));
  assert.equal(p.bridge.take(1).value, 'ক'.repeat(8));
});

test('byte limits do not split reph, vowels, decomposed marks or emoji ZWJ', () => {
  for (const cluster of ['র্ক', 'র্কি', 'কো', 'e\u0301', '👩‍🔬']) {
    const p = profile({maximum:3 + Buffer.byteLength(cluster)});
    assert.equal(p.edit('abc' + cluster), 'abc', cluster);
  }
});

test('exclusive ASCII byte maximum is preserved, including a one-byte buffer', () => {
  const p = profile({maximum:4});
  assert.equal(p.edit('abc'), 'abc');
  assert.equal(p.edit('abcd'), 'abc');
  assert.equal(profile({maximum:1}).edit('a'), '');
});

test('maximum zero leaves Unicode edits unrestricted', () => {
  const p = profile({maximum:0});
  const value = 'ক্ষুদ্র👩‍🔬e\u0301'.repeat(20);
  assert.equal(p.edit(value), value);
  assert.equal(p.bridge.take(1).value, value);
});

test('a 32-byte username accepts all ASCII bytes and keeps overflowing Bengali clusters whole', () => {
  const p = profile({maximum:33});
  assert.equal(p.edit('a'.repeat(32)), 'a'.repeat(32));
  assert.equal(p.edit('a'.repeat(33)), 'a'.repeat(32));
  assert.equal(p.edit('ক'.repeat(8) + 'ক্ষ'), 'ক'.repeat(8));
});

test('composition is neither truncated nor delivered until compositionend', () => {
  const p = profile({value:'নাম'});
  const value = 'ক'.repeat(8) + 'ক্ষ';
  p.input.fire('compositionstart');
  assert.equal(p.edit(value), value);
  assert.equal(p.bridge.take(1), null);
  p.draw('native update');
  assert.equal(p.input.value, value);
  p.input.fire('compositionend');
  assert.equal(p.input.value, 'ক'.repeat(8));
  assert.equal(p.bridge.take(1).value, 'ক'.repeat(8));
  assert.equal(p.bridge.take(1), null);
});

test('without Segmenter, overflowing Unicode retains the last accepted edit', () => {
  const p = profile({segmenter:false,value:'নাম'});
  assert.equal(p.edit('নতুন'), 'নতুন');
  assert.equal(p.edit('ক'.repeat(8) + 'ক্ষ'), 'নতুন');
  assert.equal(p.bridge.take(1).value, 'নতুন');
  assert.equal(p.edit('x'.repeat(32)), 'x'.repeat(31));
});

test('fallback accepted value follows programmatic native updates', () => {
  const p = profile({segmenter:false,value:'নাম'});
  p.draw('নতুন');
  assert.equal(p.input.value, 'নতুন');
  assert.equal(p.edit('ক'.repeat(8) + 'ক্ষ'), 'নতুন');
});

test('fallback preserves composition and unlimited input contracts', () => {
  const p = profile({segmenter:false,value:'নাম'});
  const value = '👩‍🔬'.repeat(4);
  p.input.fire('compositionstart');
  p.edit(value);
  assert.equal(p.bridge.take(1), null);
  p.input.fire('compositionend');
  assert.equal(p.bridge.take(1).value, 'নাম');
  assert.equal(profile({segmenter:false,maximum:0}).edit(value), value);
});
