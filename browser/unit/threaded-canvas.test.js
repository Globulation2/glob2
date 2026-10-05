const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
function host(canvas, threaded = true, fail = false) {
  let library, calls = 0;
  const heap = new Int32Array(16);
  const name = threaded ? 'setCanvasElementSizeCallingThread' : '_emscripten_set_canvas_element_size';
  const scope = vm.createContext({HEAP32:heap, findCanvasEventTarget:() => canvas,
    [name]:(target, width, height) => {
      calls++;
      if (!canvas) return -4;
      if (fail) throw new Error('SDK resize failed');
      if (canvas.GLctxObject?.GLctx?.isContextLost()) throw new Error('Lost viewport query');
      canvas.width = width; canvas.height = height;
      canvas.pixels = null;
      return 0;
    }, addToLibrary:value => { library = value; }});
  const source = readFileSync(path.join(__dirname, '../canvas-size.js'), 'utf8')
    .replace(/#if PTHREADS\n([\s\S]*?)#else\n([\s\S]*?)#endif/, (_, yes, no) => threaded ? yes : no);
  vm.runInContext(source, scope);
  const postset = threaded ? '$setCanvasElementSizeCallingThread__postset' : 'emscripten_set_canvas_element_size__postset';
  assert.equal(typeof library[postset], 'string');
  vm.runInContext(library[postset], scope);
  return {resize:(w,h) => scope[name]('#canvas',w,h), heap,
    calls:() => calls};
}
for (const threaded of [false, true]) {
const variant = threaded ? 'threaded' : 'serial';
test(`${variant}: redundant software canvas resizing preserves the presented pixels`, () => {
  const canvas = {width:1200,height:900,pixels:'colony'};
  const h = host(canvas, threaded);
  assert.equal(h.resize(1200,900),0);
  assert.equal(canvas.pixels,'colony');
  assert.equal(h.calls(),0);
  assert.equal(h.resize(900,1200),0);
  assert.equal(h.calls(),1);
  assert.equal(canvas.width,900);
  assert.equal(canvas.height,1200);
});
test(`${variant}: shared dimensions must already match before skipping the resize`, () => {
  const canvas = {width:1200,height:900,pixels:'colony',canvasSharedPtr:8};
  const h = host(canvas, threaded);
  h.resize(1200,900);
  assert.equal(h.calls(),1);
  h.heap[2]=1200;h.heap[3]=900;canvas.pixels='colony';
  h.resize(1200,900);
  assert.equal(h.calls(),1);
  assert.equal(canvas.pixels,'colony');
});
test(`${variant}: GPU, transferred and unknown canvas requests retain SDK handling`, () => {
  for (const extra of [{GLctxObject:{GLctx:{isContextLost:()=>false}}},
                       {controlTransferredOffscreen:true}, {offscreenCanvas:{width:1200,height:900}}]) {
    const h = host({width:1200,height:900,...extra}, threaded);h.resize(1200,900);
    assert.equal(h.calls(),1);
  }
  const h = host(null, threaded);assert.equal(h.resize(1200,900),-4);
});

test(`${variant}: lost-context resize preserves context ownership after success or failure`, () => {
  for (const fail of [false, true]) {
    const context = {GLctx:{isContextLost:() => true}};
    const canvas = {width:1200,height:900,GLctxObject:context};
    const h = host(canvas, threaded, fail);
    if (fail) assert.throws(() => h.resize(900,1200), /SDK resize failed/);
    else assert.equal(h.resize(900,1200), 0);
    assert.equal(h.calls(), 1);
    assert.equal(canvas.GLctxObject, context);
  }
});
}
