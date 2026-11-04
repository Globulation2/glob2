const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf('function browserStartupRenderer(');
const end = shell.indexOf('// End browser startup renderer.', start);
const context = vm.createContext({});
vm.runInContext(shell.slice(start, end), context);
const transport = context.browserStartupRenderer;

test('saved Software retains DOM ownership on an accelerated browser', () => {
  assert.equal(transport('rendererBackend=software\nscreenFlags=1\n', null, 'webgl2'), 'software');
});
test('saved GPU backends transfer ownership even when automatic policy chose software', () => {
  for (const backend of ['legacy-gl','programmable-gl','portable-sdl'])
    assert.equal(transport('rendererBackend='+backend+'\n', null, 'software'), 'webgl2');
});
test('URL overrides take precedence, including Automatic', () => {
  assert.equal(transport('rendererBackend=software', 'webgl2', 'webgl2'), 'webgl2');
  assert.equal(transport('rendererBackend=legacy-gl', 'software', 'webgl2'), 'software');
  assert.equal(transport('rendererBackend=software', 'auto', 'webgl2'), 'webgl2');
});
test('WebGPU retains the launcher policy for its asynchronous startup', () => {
  for (const automatic of ['software','webgl2']) {
    assert.equal(transport('rendererBackend=webgpu', null, automatic), automatic);
    assert.equal(transport(null, 'webgpu', automatic), automatic);
  }
});
test('legacy preferences match Settings screenFlags migration', () => {
  for (const flags of [0,2,4,8,16])
    assert.equal(transport('screenFlags='+flags+'\n', null, 'webgl2'), 'software');
  for (const flags of [1,32,33,35])
    assert.equal(transport('screenFlags='+flags+'\n', null, 'software'), 'webgl2');
  const header=readFileSync(path.join(__dirname,'../../../libs/gag/include/SDLGraphicContext.h'),'utf8');
  assert.match(header,/USEGPU = 1,/);assert.match(header,/PORTABLEGPU = 32,/);
});
test('fresh, Automatic and unrecognized preferences retain the automatic policy', () => {
  for (const text of [null,'','rendererBackend=auto','rendererBackend=unknown\nscreenFlags=0'])
    for (const automatic of ['software','webgl2'])
      assert.equal(transport(text,null,automatic),automatic);
});
