const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
function host(canvas) {
  let library, calls = 0;
  const heap = new Int32Array(16);
  const scope = vm.createContext({HEAP32:heap, findCanvasEventTarget:() => canvas,
    setCanvasElementSizeCallingThread:(target, width, height) => {
      calls++;
      if (!canvas) return -4;
      canvas.width = width; canvas.height = height;
      canvas.pixels = null;
      return 0;
    }, addToLibrary:value => { library = value; }});
  vm.runInContext(readFileSync(path.join(__dirname, '../threaded-egl.js'), 'utf8'), scope);
  vm.runInContext(library.$setCanvasElementSizeCallingThread__postset, scope);
  return {resize:(w,h) => scope.setCanvasElementSizeCallingThread('#canvas',w,h), heap,
    calls:() => calls};
}
test('redundant software canvas resizing preserves the presented pixels', () => {
  const canvas = {width:1200,height:900,pixels:'colony'};
  const h = host(canvas);
  assert.equal(h.resize(1200,900),0);
  assert.equal(canvas.pixels,'colony');
  assert.equal(h.calls(),0);
  assert.equal(h.resize(900,1200),0);
  assert.equal(h.calls(),1);
  assert.equal(canvas.width,900);
  assert.equal(canvas.height,1200);
});
test('shared dimensions must already match before skipping the resize', () => {
  const canvas = {width:1200,height:900,pixels:'colony',canvasSharedPtr:8};
  const h = host(canvas);
  h.resize(1200,900);
  assert.equal(h.calls(),1);
  h.heap[2]=1200;h.heap[3]=900;canvas.pixels='colony';
  h.resize(1200,900);
  assert.equal(h.calls(),1);
  assert.equal(canvas.pixels,'colony');
});
test('GPU, transferred and unknown canvas requests retain SDK handling', () => {
  for (const extra of [{GLctxObject:{GLctx:{isContextLost:()=>false}}},
                       {controlTransferredOffscreen:true}, {offscreenCanvas:{width:1200,height:900}}]) {
    const h = host({width:1200,height:900,...extra});h.resize(1200,900);
    assert.equal(h.calls(),1);
  }
  const h = host(null);assert.equal(h.resize(1200,900),-4);
});
