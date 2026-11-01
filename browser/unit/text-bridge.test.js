const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf('Module.textBridge = (() => {');
const end = shell.indexOf('\n})();', start) + '\n})();'.length;
assert.ok(start >= 0 && end > start);

function profile({maximum = 32, maxCharacters = 0, value = '', segmenter = true} = {}) {
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
  const draw = next => bridge.field(1, rect, 400, 200, next, false, maximum, rect, false, maxCharacters);
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

test('a character-limited account name accepts all 32 Bengali codepoints', () => {
  const p = profile({maximum:0,maxCharacters:32});
  const name = 'ক'.repeat(32);
  assert.equal(Buffer.byteLength(name), 96);
  assert.equal(p.edit(name), name);
  assert.equal(p.bridge.take(1).value, name);
  assert.equal(p.edit(name + 'ক'), name);
});

test('a 128-character map title accepts 43 Bengali characters and all 128', () => {
  const p = profile({maximum:0,maxCharacters:128});
  const title = 'ক'.repeat(43);
  assert.equal(Buffer.byteLength(title), 129);
  assert.equal(p.edit(title), title);
  const full = 'ক'.repeat(128);
  assert.equal(p.edit(full), full);
  assert.equal(p.edit(full + 'ক'), full);
});

test('character bounds count Unicode codepoints rather than UTF-16 code units', () => {
  const p = profile({maximum:0,maxCharacters:32});
  const value = '😀'.repeat(32);
  assert.equal(value.length, 64);
  assert.equal(p.edit(value), value);
  assert.equal(p.edit(value + '😀'), value);
});

test('character overflow preserves complete conjuncts, reph, marks and emoji ZWJ', () => {
  for (const cluster of ['ক্ষ','র্কি','e\u0301','👩‍🔬']) {
    const p = profile({maximum:0,maxCharacters:3 + Array.from(cluster).length - 1});
    assert.equal(p.edit('abc' + cluster), 'abc', cluster);
  }
});

test('character and byte bounds are both enforced independently', () => {
  assert.equal(profile({maximum:33,maxCharacters:32}).edit('ক'.repeat(32)), 'ক'.repeat(10));
  assert.equal(profile({maximum:100,maxCharacters:2}).edit('ক'.repeat(3)), 'ক'.repeat(2));
  assert.equal(profile({maximum:4,maxCharacters:10}).edit('abcdef'), 'abc');
  assert.equal(profile({maximum:100,maxCharacters:3}).edit('abcdef'), 'abc');
});

test('explicit chat and LAN byte limits retain their inclusive payload capacities', () => {
  for (const capacity of [254,256]) {
    const p = profile({maximum:capacity + 1});
    assert.equal(p.edit('x'.repeat(capacity)), 'x'.repeat(capacity));
    assert.equal(p.edit('x'.repeat(capacity + 1)), 'x'.repeat(capacity));
  }
});

test('character limits wait for compositionend before delivering a whole prefix', () => {
  const p = profile({maximum:0,maxCharacters:5,value:'abc'});
  const value = 'abcক্ষ';
  p.input.fire('compositionstart');
  assert.equal(p.edit(value), value);
  assert.equal(p.bridge.take(1), null);
  p.input.fire('compositionend');
  assert.equal(p.bridge.take(1).value, 'abc');
});

test('without Segmenter character limits retain accepted Unicode and native updates', () => {
  const p = profile({maximum:0,maxCharacters:3,segmenter:false,value:'ক'});
  assert.equal(p.edit('কখ'), 'কখ');
  assert.equal(p.bridge.take(1).value, 'কখ');
  p.draw('নাম');
  assert.equal(p.edit('ক্ষক'), 'নাম');
  assert.equal(p.edit('abcd'), 'abc');
  assert.equal(profile({maximum:4,maxCharacters:2,segmenter:false}).edit('abcd'), 'ab');
});
