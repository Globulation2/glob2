const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {create} = require('../presentation-surface');
function environment(mode = 'serial', stableHost = false) {
  const events = new Map();
  const document = {activeElement:null, fullscreenElement:null,
    addEventListener:(name,fn)=>events.set(name,fn),
    removeEventListener:(name,fn)=>{if(events.get(name)===fn)events.delete(name);},
    dispatchEvent:name=>events.get(name)?.()};
  const fullscreenHost = stableHost ? {id:'presentation-host',requestFullscreen:()=>{document.fullscreenElement=fullscreenHost;}} : null;
  const parent = {canvas:null};
  function element() {
    return {id:'canvas',width:800,height:600,style:{width:'1200px'},tabIndex:0,ariaLabel:'Globulation 2',
      listeners:new Map(),
      cloneNode() {const next=element();next.style={...this.style};return next;},
      replaceWith(next) {parent.canvas=next;},
      focus() {document.activeElement=this;},
      transferControlToOffscreen() {return {width:this.width,height:this.height};},
      addEventListener(name,fn) {this.listeners.set(name,fn);},
      removeEventListener(name,fn) {if(this.listeners.get(name)===fn)this.listeners.delete(name);}
    };
  }
  const canvas=element();parent.canvas=canvas;document.activeElement=canvas;
  const module={canvas,executionMode:mode,renderer:'software',SDL3:{ctx:'old',ctxCanvas:canvas,image:'old'},gpuLost:true};
  return {document,parent,module,fullscreenHost,manager:create(module,{document,fullscreenHost})};
}
test('serial replacement restores dimensions, DOM metadata, focus and runtime references',()=>{
  const {manager,module,document,parent}=environment();const original=module.canvas;
  original.width=original.height=0; // SDL_DestroyWindow has already retired it.
  const surface=manager.prepare({gpu:true,width:900,height:700});
  assert.equal(surface.offscreenCanvas,null);assert.equal(parent.canvas,module.canvas);
  assert.notEqual(module.canvas,original);assert.equal(module.canvas.width,900);
  assert.equal(module.canvas.style.width,'1200px');assert.equal(module.canvas.ariaLabel,'Globulation 2');
  assert.equal(document.activeElement,module.canvas);assert.equal(module.SDL3.ctx,null);
  assert.equal(module.gpuLost,false);assert.equal(module.visibilityPending,true);
  manager.finish(true);assert.equal(module.renderer,'webgl2');assert.equal(manager.generation,1);
});
test('repeated switches and rollback remove listeners from every retired canvas',()=>{
  const {manager,module}=environment();let installed=0,removed=0;
  manager.bind(canvas=>{installed++;const fn=()=>{};canvas.addEventListener('keydown',fn);return ()=>{removed++;canvas.removeEventListener('keydown',fn);};});
  for(let i=0;i<20;i++) {const old=module.canvas;manager.prepare({gpu:!!(i%2),width:800,height:600});assert.equal(old.listeners.size,0);manager.finish(true);}
  const original=module.canvas;manager.prepare({gpu:true,width:400,height:300});const rejected=module.canvas;manager.finish(false);
  assert.equal(module.canvas,original);assert.equal(rejected.listeners.size,0);assert.equal(original.listeners.size,1);
  assert.equal(installed,removed+1);assert.equal(manager.generation,20);
});
test('threaded GPU owns fresh offscreen context; threaded software remains DOM',()=>{
  const {manager,module}=environment('threaded');
  const gpu=manager.prepare({gpu:true,width:800,height:600,sharedPtr:16});
  assert.ok(gpu.offscreenCanvas);assert.equal(module.canvas.controlTransferredOffscreen,true);
  assert.equal(module.canvas.canvasSharedPtr,16);manager.finish(true);
  const cpu=manager.prepare({gpu:false,width:800,height:600});
  assert.equal(cpu.offscreenCanvas,null);assert.equal(module.canvas.controlTransferredOffscreen,undefined);manager.finish(true);
});
test('fullscreen, invalid dimensions, transfer failures and overlapping requests retain previous canvas',()=>{
  const {manager,module,document}=environment('threaded');const original=module.canvas;
  document.fullscreenElement=original;assert.throws(()=>manager.prepare({gpu:false,width:800,height:600}),/fullscreen/);document.fullscreenElement=null;
  assert.throws(()=>manager.prepare({gpu:true,width:800,height:600}),/transfer unavailable/);
  assert.throws(()=>manager.prepare({gpu:false,width:0,height:600}),/dimensions/);
  assert.equal(module.canvas,original);manager.prepare({gpu:false,width:800,height:600});
  assert.throws(()=>manager.prepare({gpu:false,width:800,height:600}),/already active/);manager.finish(false);
  assert.equal(module.canvas,original);
});
test('stable fullscreen ancestor survives replacement and restores current canvas dimensions',()=>{
  const {manager,module,document,fullscreenHost}=environment('serial',true);
  const before=module.canvas;
  const resized=[];manager.fullscreenRestore(c=>[c.width,c.height],(c,w,h)=>{resized.push(c);c.width=w;c.height=h;});
  before.requestFullscreen();assert.equal(document.fullscreenElement,fullscreenHost);
  manager.retire();assert.equal(manager.retiring,true);
  manager.prepare({gpu:true,width:900,height:700});manager.finish(true);
  assert.equal(document.fullscreenElement,fullscreenHost);assert.equal(manager.retiring,false);
  const replacement=module.canvas;assert.notEqual(replacement,before);
  document.fullscreenElement=null;document.dispatchEvent('fullscreenchange');
  assert.deepEqual(resized,[replacement]);assert.equal(replacement.width,800);assert.equal(replacement.height,600);
});
function library(threaded, canvas=null) {
  let library;const listeners=[],messages=[],freed=[];
  const scope=vm.createContext({Module:{canvas},GL:{offscreenCanvases:canvas?{canvas:{offscreenCanvas:canvas,canvasSharedPtr:32,id:'canvas'}}:{}},HEAP32:new Int32Array(64),
    ENVIRONMENT_IS_PTHREAD:threaded,_malloc:()=>64,_free:p=>freed.push(p),_pthread_self:()=>128,
    addEventListener:(type,fn)=>listeners.push(fn),postMessage:m=>messages.push(m),
    addToLibrary:l=>{if(l.$Glob2SurfaceBridge)library=l;},stringToUTF8:()=>{}});
  const source=readFileSync(path.join(__dirname,'../canvas-size.js'),'utf8');
  let enabled=true,stack=[];const output=[];
  for(const line of source.split('\n')) {
    if(line.startsWith('#if PTHREADS')) {stack.push(enabled);enabled=enabled&&threaded;}
    else if(line.startsWith('#else'))enabled=stack.at(-1)&&!threaded;
    else if(line.startsWith('#endif'))enabled=stack.pop();
    else if(enabled)output.push(line);
  }
  vm.runInContext(output.join('\n'),scope);scope.Glob2SurfaceBridge=library.$Glob2SurfaceBridge;
  return {bridge:scope.Glob2SurfaceBridge,scope,messages,freed,library,
    webgpuAvailable:library.glob2_surface_webgpu_available,
    reply:r=>listeners.forEach(fn=>fn({data:{glob2Surface:r}}))};
}
for (const threaded of [false,true]) test(`WebGPU availability follows the ${threaded?'worker':'serial'} presentation realm`,()=>{
  const {scope,webgpuAvailable}=library(threaded);
  assert.equal(webgpuAvailable(),0);
  scope.navigator={gpu:{requestAdapter(){}}};
  assert.equal(webgpuAvailable(),1);
  scope.navigator={};
  assert.equal(webgpuAvailable(),0);
});
test('GL bridge restores legacy texture tuning and copies readback without a shared view',()=>{
  const {scope,library:l}=library(true);
  scope.GLctx={MAX_TEXTURE_IMAGE_UNITS:0x8872,getParameter:()=>4,
    getBufferSubData:(target,offset,data)=>{
      assert.equal(target,0x88EB);assert.equal(offset,0);
      assert.equal(Object.prototype.toString.call(data.buffer),'[object ArrayBuffer]');
      data.set([19,23,29,31]);
    }};
  assert.equal(l.glob2_gl_override_texture_units(6),-1);
  assert.equal(scope.Module.GL_MAX_TEXTURE_IMAGE_UNITS,4);
  assert.equal(l.glob2_gl_override_texture_units(2),4);
  l.glob2_gl_restore_texture_units(4);
  assert.equal(scope.Module.GL_MAX_TEXTURE_IMAGE_UNITS,4);
  l.glob2_gl_restore_texture_units(-1);
  assert.equal('GL_MAX_TEXTURE_IMAGE_UNITS' in scope.Module,false);
  scope.HEAPU8=new Uint8Array(new SharedArrayBuffer(16));
  l.glob2_gl_copy_readback(3,4);
  assert.deepEqual(Array.from(scope.HEAPU8.slice(3,7)),[19,23,29,31]);
  scope.Module.renderer='webgl2';assert.equal(l.glob2_surface_startup_uses_gl(),1);
  scope.Module.renderer='software';assert.equal(l.glob2_surface_startup_uses_gl(),0);
});
test('pthread reply installs SDK offscreen map and rollback restores previous ownership',()=>{
  const original={id:'canvas'};const {bridge,scope,messages,freed,reply}=library(true,original);
  assert.equal(bridge.prepare(1,900,700),0);assert.deepEqual(Array.from(scope.HEAP32.slice(16,19)),[900,700,128]);
  const request=messages[0].glob2Surface.request;const next={width:900,height:700};
  reply({action:'ready',request,id:'canvas',offscreenCanvas:next,canvasSharedPtr:64});
  assert.equal(bridge.state,1);assert.equal(scope.Module.canvas,next);assert.equal(scope.GL.offscreenCanvases.canvas.offscreenCanvas,next);
  bridge.finish(false);assert.equal(scope.Module.canvas,original);assert.deepEqual(freed,[64]);
  assert.equal(messages[1].glob2Surface.success,false);
});
test('pthread software release removes map and commit frees old shared dimensions',()=>{
  const {bridge,scope,freed,reply}=library(true,{id:'canvas'});bridge.prepare(0,800,600);
  reply({action:'ready',request:1,id:'canvas',offscreenCanvas:null,canvasSharedPtr:0});
  assert.equal(scope.GL.offscreenCanvases.canvas,undefined);assert.equal(scope.Module.canvas,null);
  bridge.finish(true);assert.deepEqual(freed,[32]);assert.equal(bridge.pending,null);
});
test('stale pthread replies are ignored; failed transfer releases new shared allocation',()=>{
  const original={id:'canvas'};const {bridge,scope,freed,reply}=library(true,original);bridge.prepare(1,800,600);
  reply({action:'ready',request:99,id:'canvas',offscreenCanvas:{}});assert.equal(bridge.state,0);
  reply({action:'failed',request:1,error:'transfer rejected'});
  assert.equal(bridge.state,-1);assert.equal(bridge.failure,'transfer rejected');assert.deepEqual(freed,[64]);
  assert.equal(scope.Module.canvas,original);assert.equal(bridge.pending,null);
});
