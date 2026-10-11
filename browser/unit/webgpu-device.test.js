// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test');
const assert=require('node:assert/strict');
const vm=require('node:vm');
const fs=require('node:fs');
const path=require('node:path');
function fixture(){
  const library={},uploads=[],completion=[],maps=[],lost=[];
  let destroyed=0;
  const device={limits:{maxTextureDimension2D:2048},lost:new Promise(r=>lost.push(r)),addEventListener(){},
    createShaderModule(){return{getCompilationInfo:async()=>({messages:[]})};},
    createRenderPipelineAsync:async()=>({getBindGroupLayout:()=>({})}),createRenderPipeline:()=>({getBindGroupLayout:()=>({})}),
    createTexture(){return{createView:()=>({}),destroy(){destroyed++;}};},createSampler:()=>({}),createBindGroup:()=>({}),
    createBuffer({size}){const data=new ArrayBuffer(size);return{size,destroy(){destroyed++;},mapAsync:()=>new Promise(r=>maps.push(r)),getMappedRange:()=>data,unmap(){}};},
    createCommandEncoder(){return{beginRenderPass:()=>({setPipeline(){},setViewport(){},setScissorRect(){},setVertexBuffer(){},setBindGroup(){},setIndexBuffer(){},draw(){},drawIndexed(){},end(){}}),finish:()=>({}),copyTextureToBuffer(){},copyTextureToTexture(){}};},
    queue:{writeBuffer(){},writeTexture(target,data){uploads.push(data);},submit(){},onSubmittedWorkDone:()=>new Promise(r=>completion.push(r))}};
  const canvas={getContext:()=>({configure(){},unconfigure(){},getCurrentTexture:()=>device.createTexture()})};
  const context={addToLibrary:value=>Object.assign(library,value),navigator:{gpu:{requestAdapter:async()=>({requestDevice:async()=>device}),getPreferredCanvasFormat:()=> 'bgra8unorm'}},
    FS:{readFile:p=>p.endsWith('metadata.json')?fs.readFileSync(path.join(__dirname,'../../libgag/shaders/renderer-manifest.json'),'utf8'):''},Module:{canvas},HEAPU8:new Uint8Array(new SharedArrayBuffer(65536)),GPUTextureUsage:{TEXTURE_BINDING:1,RENDER_ATTACHMENT:2,COPY_SRC:4,COPY_DST:8},GPUBufferUsage:{VERTEX:1,INDEX:2,UNIFORM:4,COPY_DST:8,MAP_READ:16},GPUMapMode:{READ:1}};
  vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(__dirname,'../webgpu-device.js'),'utf8'),context);context.Glob2WebGPU=library.$Glob2WebGPU;
  return{library,g:context.Glob2WebGPU,context,device,uploads,completion,maps,lost,destroyed:()=>destroyed};
}
test('prepare validates shaders before presentation attachment and copies shared uploads',async()=>{
  const f=fixture();await f.g.prepare('/shaders');assert.equal(f.g.state,1);assert.equal(f.library.glob2_webgpu_attach(),2048);
  f.context.HEAPU8.set([10,20,30,255]);assert.equal(f.library.glob2_webgpu_upload(1,1,1,0,0,0),1);f.context.HEAPU8.fill(0,0,4);
  assert.deepEqual(Array.from(f.uploads.at(-1)),[10,20,30,255]);assert.equal(f.uploads.at(-1).buffer instanceof SharedArrayBuffer,false);
});
test('submission admission and GPU buffer retirement are bounded by completion',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();
  for(let i=1;i<=8;i++){assert.equal(f.g.submit(new Uint8Array(),0,new Uint8Array(),new Uint8Array(),16,16),i);f.g.finishPresentation([]);}
  assert.equal(f.library.glob2_webgpu_can_begin(),0);assert.equal(f.library.glob2_webgpu_present(),1);assert.equal(f.g.frames.length,8);assert.equal(f.g.submit(new Uint8Array(),0,new Uint8Array(),new Uint8Array(),16,16),0);
  assert.equal(f.destroyed(),0);f.completion.forEach(resolve=>resolve());await Promise.resolve();
  assert.equal(f.g.frames.length,0);assert.equal(f.g.completed,8);assert.equal(f.destroyed(),8);assert.equal(f.library.glob2_webgpu_can_begin(),1);
});
test('capture polls submission order despite reversed async mapping completion',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  for(let i=0;i<3;i++)assert.equal(f.g.capture(i+10,16,16),1);assert.equal(f.g.capture(13,16,16),0);
  f.maps[1]();await Promise.resolve();assert.equal(f.library.glob2_webgpu_capture_poll(4096,64),0);
  f.maps[0]();await Promise.resolve();assert.equal(f.g.captures[0].identity,10);assert.equal(f.library.glob2_webgpu_capture_poll(4096,64),1);
  assert.equal(f.g.captures[0].identity,11);assert.equal(f.library.glob2_webgpu_capture_poll(4096,64),1);
  f.library.glob2_webgpu_capture_cancel();assert.equal(f.g.captures.length,0);f.maps[2]();await Promise.resolve();
});
test('resource reset preserves device-loss notification',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.library.glob2_webgpu_reset();
  f.lost[0]({message:'injected loss'});await Promise.resolve();assert.equal(f.library.glob2_webgpu_poll(),-1);assert.equal(f.library.glob2_webgpu_can_begin(),-1);assert.match(f.g.error,/injected loss/);
});

test('many flushed chunks reserve one presentation slot until the presentation fence',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();
  for(let i=1;i<=32;i++)assert.equal(f.g.submit(new Uint8Array(),0,new Uint8Array(),new Uint8Array(),16,16),i);
  assert.equal(f.g.frames.length,0);assert.equal(f.completion.length,0);assert.equal(f.g.completed,0);assert.equal(f.library.glob2_webgpu_can_begin(),1);
  assert.equal(f.g.transientBytes,32*4);f.g.finishPresentation([]);assert.equal(f.g.frames.length,1);
  f.completion[0]();await Promise.resolve();assert.equal(f.g.completed,32);assert.equal(f.g.transientBytes,0);assert.equal(f.destroyed(),32);
});
test('exported capture preserves target and crop; aggregate memory has a hard bound',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  f.g.resources.set(7,{texture:f.device.createTexture(),width:32,height:32});
  assert.equal(f.library.glob2_webgpu_capture(90,8,8,7,16,16),1);assert.equal(f.g.captures[0].width,8);assert.equal(f.library.glob2_webgpu_capture(91,8,8,7,30,30),0);
  f.g.bytes=256*1024*1024;assert.throws(()=>f.g.reserve(1),/memory budget/);assert.equal(f.g.capture(92,16,16),0);
});

test('budget deferral preserves prior presentation and admitted captures',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  f.g.submit(new Uint8Array(),0,new Uint8Array(),new Uint8Array(),16,16);f.g.finishPresentation([]);f.completion.shift()();await Promise.resolve();
  assert.equal(f.g.capture(8,16,16),1);f.g.submit(new Uint8Array(),0,new Uint8Array(),new Uint8Array(),16,16);
  f.g.bytes=256*1024*1024;assert.equal(f.library.glob2_webgpu_submit(0,0,0,0,0,0,16,16),0);assert.equal(f.library.glob2_webgpu_deferred(),1);assert.equal(f.g.lost,false);
  const abortedIdentity=f.g.issued+1;assert.equal(f.library.glob2_webgpu_discard(),abortedIdentity);assert.equal(f.g.captures.length,1);assert.equal(f.g.activeFrame,null);assert.equal(f.library.glob2_webgpu_deferred(),0);
  f.completion.forEach(r=>r());await Promise.resolve();assert.equal(f.g.transientBytes,0);
});

test('owned shader text prepares without accessing the worker-local filesystem',async()=>{
  const f=fixture(),metadata=JSON.parse(f.context.FS.readFile('/metadata.json'));
  f.context.FS.readFile=()=>{throw Error('worker has no preload');};
  const stages={};for(const variant of['primitive','world','cloud','point','skin','skin-rig','terrain-compose','terrain-reduce'])stages[variant]={vert:'',frag:''};
  await f.g.prepare('/shaders',{metadata,stages});assert.equal(f.g.state,1);assert.equal(f.g.error,'');assert.equal(f.library.glob2_webgpu_shader_payload__proxy,'sync');
});
test('discard fences preparation uploads even before the first packet submission',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();
  const buffer=f.g.buffer(new Uint8Array(16),f.context.GPUBufferUsage.UNIFORM);
  assert.equal(f.g.issued,0);assert.equal(f.library.glob2_webgpu_discard(),1);
  assert.equal(f.g.completed,0);assert.equal(f.g.transientBytes,16);
  f.completion.shift()();await Promise.resolve();assert.equal(f.g.completed,1);assert.equal(f.g.transientBytes,0);
});

test('device loss during asynchronous preparation cannot publish readiness',async()=>{
  const f=fixture();let finishPipeline,signal;
  const waiting=new Promise(resolve=>{signal=resolve;});
  f.device.createRenderPipelineAsync=()=>{
    if(finishPipeline)return Promise.resolve({getBindGroupLayout:()=>({})});
    return new Promise(resolve=>{finishPipeline=resolve;signal();});
  };
  const preparing=f.g.prepare('/shaders');await waiting;
  f.lost[0]({message:'loss during pipeline creation'});await Promise.resolve();
  assert.equal(f.library.glob2_webgpu_poll(),-1);
  finishPipeline({getBindGroupLayout:()=>({})});await preparing;
  assert.equal(f.library.glob2_webgpu_poll(),-1);
  assert.match(f.g.error,/loss during pipeline creation/);
});


test('superseded preparation cannot poison or overwrite a new device',async()=>{
  const f=fixture(),newDevice=fixture().device;let release,started;
  const waiting=new Promise(resolve=>{started=resolve;});
  f.device.createRenderPipelineAsync=()=>new Promise(resolve=>{release=resolve;started();});
  const old=f.g.prepare('/old');await waiting;
  f.lost[0]({message:'old preparation lost'});await Promise.resolve();
  f.context.navigator.gpu.requestAdapter=async()=>({requestDevice:async()=>newDevice});
  await f.g.prepare('/new');const pipelines=Array.from(f.g.pipelines.values()),present=f.g.presentPipeline;
  assert.equal(f.g.state,1);assert.equal(f.g.device,newDevice);
  release({old:true});await old;
  assert.equal(f.g.state,1);assert.equal(f.g.lost,false);assert.equal(f.g.device,newDevice);
  assert.deepEqual(Array.from(f.g.pipelines.values()),pipelines);assert.equal(f.g.presentPipeline,present);
});

test('device acquired by superseded preparation is destroyed without replacing the owner',async()=>{
  const f=fixture(),newDevice=fixture().device;let release,started,destroyed=0;
  const waiting=new Promise(resolve=>{started=resolve;});
  f.context.navigator.gpu.requestAdapter=async()=>({requestDevice:()=>new Promise(resolve=>{release=resolve;started();})});
  const old=f.g.prepare('/old');await waiting;
  f.g.fail('cancel preparation');
  f.context.navigator.gpu.requestAdapter=async()=>({requestDevice:async()=>newDevice});
  await f.g.prepare('/new');release({destroy(){destroyed++;}});await old;
  assert.equal(destroyed,1);assert.equal(f.g.device,newDevice);assert.equal(f.g.state,1);assert.equal(f.g.lost,false);
});
