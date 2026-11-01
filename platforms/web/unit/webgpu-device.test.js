// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test');
const assert=require('node:assert/strict');
const vm=require('node:vm');
const fs=require('node:fs');
const path=require('node:path');
function fixture(){
  const library={},uploads=[],completion=[],rejections=[],maps=[],lost=[];
  let destroyed=0;const calls={buffers:0,groups:0,views:0,writes:0,draws:[],passes:[],ends:0,pipeline:0,viewport:0,scissor:0,vertex:0,bindings:0,index:0,sequence:[]};
  const device={limits:{maxTextureDimension2D:2048},lost:new Promise(r=>lost.push(r)),addEventListener(){},
    createShaderModule(){return{getCompilationInfo:async()=>({messages:[]})};},
    createRenderPipelineAsync:async()=>({getBindGroupLayout:()=>({})}),createRenderPipeline:()=>({getBindGroupLayout:()=>({})}),
    createTexture(){return{createView:()=>{calls.views++;return{};},destroy(){destroyed++;}};},createSampler:()=>({}),createBindGroup:description=>{calls.groups++;return{description};},
    createBuffer({size}){calls.buffers++;const data=new ArrayBuffer(size);return{size,destroy(){destroyed++;},mapAsync:()=>new Promise(r=>maps.push(r)),getMappedRange:()=>data,unmap(){}};},
    createCommandEncoder(){return{beginRenderPass:description=>{calls.passes.push(description);calls.sequence.push('begin');return{setPipeline(){calls.pipeline++;},setViewport(){calls.viewport++;},setScissorRect(){calls.scissor++;},setVertexBuffer(){calls.vertex++;},setBindGroup(){calls.bindings++;},setIndexBuffer(){calls.index++;},draw(...args){calls.draws.push(args);calls.sequence.push('draw');},drawIndexed(...args){calls.draws.push(args);calls.sequence.push('indexed');},end(){calls.ends++;calls.sequence.push('end');}};},finish:()=>({}),copyTextureToBuffer(){},copyTextureToTexture(){}};},
    queue:{writeBuffer(){calls.writes++;},writeTexture(target,data){uploads.push(data);},submit(){},onSubmittedWorkDone:()=>new Promise((resolve,reject)=>{completion.push(resolve);rejections.push(reject);})}};
  const canvas={getContext:()=>({configure(){},unconfigure(){},getCurrentTexture:()=>device.createTexture()})};
  const context={addToLibrary:value=>Object.assign(library,value),navigator:{gpu:{requestAdapter:async()=>({requestDevice:async()=>device}),getPreferredCanvasFormat:()=> 'bgra8unorm'}},
    FS:{readFile:p=>p.endsWith('metadata.json')?fs.readFileSync(path.join(__dirname,'../../libs/gag/shaders/renderer-manifest.json'),'utf8'):''},Module:{canvas},HEAPU8:new Uint8Array(new SharedArrayBuffer(65536)),GPUTextureUsage:{TEXTURE_BINDING:1,RENDER_ATTACHMENT:2,COPY_SRC:4,COPY_DST:8},GPUBufferUsage:{VERTEX:1,INDEX:2,UNIFORM:4,COPY_DST:8,MAP_READ:16},GPUMapMode:{READ:1}};
  vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(__dirname,'../webgpu-device.js'),'utf8'),context);context.Glob2WebGPU=library.$Glob2WebGPU;
  return{calls,library,g:context.Glob2WebGPU,context,device,uploads,completion,rejections,maps,lost,destroyed:()=>destroyed};
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

test('retired completion rejection cannot fail a replacement allocation epoch',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();
  f.g.submit(new Uint8Array(),0,new Uint8Array(),new Uint8Array(),16,16);f.g.finishPresentation([]);
  f.library.glob2_webgpu_reset();f.rejections[0](Error('retired fence failed'));
  await Promise.resolve();await Promise.resolve();
  assert.equal(f.library.glob2_webgpu_poll(),1);assert.equal(f.g.lost,false);
  f.g.submit(new Uint8Array(),0,new Uint8Array(),new Uint8Array(),16,16);f.g.finishPresentation([]);
  f.rejections[1](Error('current fence failed'));await Promise.resolve();await Promise.resolve();
  assert.equal(f.library.glob2_webgpu_poll(),-1);assert.match(f.g.error,/current fence failed/);
});

function primitiveRecords(count){
  const records=new Uint8Array(count*256),v=new DataView(records.buffer);
  for(let i=0;i<count;i++){
    const base=i*256;v.setUint32(base+36,32,true);v.setUint32(base+48,i,true);v.setUint32(base+52,1,true);
    for(const offset of[72,76,88,92])v.setUint32(base+offset,16,true);
    for(const offset of[96,100])v.setFloat32(base+offset,16,true);
    v.setFloat32(base+104,1,true);v.setFloat32(base+120,1,true);v.setFloat32(base+124,1,true);
  }return records;
}
test('repeated ordered primitives reuse bindings and uniforms within a bounded chunk',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  const before={...f.calls};f.g.submit(primitiveRecords(4096),4096,new Uint8Array(32),new Uint8Array(),16,16);
  assert.equal(f.calls.buffers-before.buffers,3); // one geometry, two immutable uniform buffers
  assert.equal(f.calls.groups-before.groups,4); // rather than 4*4096 groups
  assert.equal(f.calls.writes-before.writes,3);
  assert.equal(f.calls.passes.length,1);assert.equal(f.calls.ends-before.ends,1);
  for(const key of ['pipeline','viewport','scissor','vertex'])assert.equal(f.calls[key]-before[key],1,key);
  assert.equal(f.calls.bindings-before.bindings,4);
  assert.equal(f.calls.draws.length,4096);assert.deepEqual(f.calls.draws.map(v=>v[2]),Array.from({length:4096},(_,i)=>i));
  assert.equal(f.g.transientBytes,80);f.g.finishPresentation([]);f.completion[0]();await Promise.resolve();assert.equal(f.g.transientBytes,0);
});
test('changed uniforms and physical image replacement cannot reuse stale bindings',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  const records=primitiveRecords(3),v=new DataView(records.buffer);v.setFloat32(256+104,.5,true);
  const before=f.calls.buffers;f.g.submit(records,3,new Uint8Array(32),new Uint8Array(),16,16);assert.equal(f.calls.buffers-before,4);
  const texture=f.device.createTexture(),old=f.g.textureView(texture);assert.equal(f.g.textureView(texture),old);
  assert.notEqual(f.g.textureView(f.device.createTexture()),old);
  const groups=f.calls.groups;f.g.submit(primitiveRecords(1),1,new Uint8Array(32),new Uint8Array(),16,16);assert.equal(f.calls.groups-groups,4); // separate chunk never reuses transient bindings
});
test('presentation binding reuse ends at resize and allocation reset',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  const before=f.calls.groups,writes=f.calls.writes;f.g.present();assert.equal(f.calls.groups-before,4);assert.equal(f.calls.writes-writes,2);
  f.g.present();assert.equal(f.calls.groups-before,4);assert.equal(f.calls.writes-writes,2);
  f.g.size(32,32);f.g.present();assert.equal(f.calls.groups-before,8);assert.equal(f.calls.writes-writes,4);
  f.g.reset();assert.equal(f.g.presentGroups,null);
});

test('uniform cache limit falls back without growing retained comparison storage',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();const owned=[],bindings=f.g.submissionBindings(owned);
  const first=bindings.uniform(new Uint32Array([0,0,0,0]));
  for(let i=1;i<=512;i++)bindings.uniform(new Uint32Array([i,0,0,0]));
  assert.equal(bindings.uniform(new Uint32Array([0,0,0,0])),first);
  const before=f.calls.buffers;bindings.uniform(new Uint32Array([512,0,0,0]));assert.equal(f.calls.buffers-before,1);
  f.g.retireBuffers(owned);assert.equal(f.g.transientBytes,0);
});

test('barriers clears and attachment changes end adjacent pass reuse in painter order',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  f.g.resources.set(7,f.g.image(16,16,null,true));
  const records=primitiveRecords(7),v=new DataView(records.buffer);
  v.setUint32(256,3,true); // explicit barrier
  v.setUint32(3*256,2,true);v.setUint32(3*256+244,1,true); // full color clear
  v.setUint32(5*256+28,7,true); // target switch and then return to screen
  f.g.submit(records,7,new Uint8Array(32),new Uint8Array(),16,16);
  assert.equal(f.calls.passes.length,6);assert.equal(f.calls.ends,6);
  assert.deepEqual(f.calls.sequence,['begin','draw','end','begin','draw','end','begin','end','begin','draw','end','begin','draw','end','begin','draw','end']);
  assert.equal(f.calls.passes[2].colorAttachments[0].loadOp,'clear');
  assert.notEqual(f.calls.passes[4].colorAttachments[0].view,f.calls.passes[5].colorAttachments[0].view);
});
test('changed state rebinds within the pass while shaders invalidate binding state',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  const records=primitiveRecords(4),v=new DataView(records.buffer);
  v.setUint32(256+72,8,true);v.setUint32(256+88,8,true);v.setUint32(256+32,32,true);
  v.setFloat32(256+104,.5,true);
  v.setUint32(2*256+4,3,true); // SmoothPoint shader, then primitive again
  f.g.submit(records,4,new Uint8Array(64),new Uint8Array(),16,16);
  assert.equal(f.calls.passes.length,1);assert.equal(f.calls.ends,1);assert.equal(f.calls.pipeline,3);
  assert.equal(f.calls.viewport,3);assert.equal(f.calls.scissor,3);assert.equal(f.calls.vertex,3);
  assert.equal(f.calls.bindings,13); // 4 initial + changed fragment + 4 point + 4 primitive
  assert.equal(f.calls.draws.length,4);
});
test('terrain ends an active pass before composing and fresh chunks reset encoded state',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  f.g.terrain=()=>{assert.equal(f.calls.ends,1);f.calls.sequence.push('terrain');};
  const records=primitiveRecords(3),v=new DataView(records.buffer);v.setUint32(256,4,true);
  f.g.submit(records,3,new Uint8Array(32),new Uint8Array(),16,16);
  assert.deepEqual(f.calls.sequence,['begin','draw','end','terrain','begin','draw','end']);
  f.g.submit(primitiveRecords(1),1,new Uint8Array(32),new Uint8Array(),16,16);
  assert.equal(f.calls.passes.length,3);assert.equal(f.calls.pipeline,3);assert.equal(f.calls.bindings,12);
});

test('indexed mesh ranges reuse physical buffers but refresh changed bounds',async()=>{
  const f=fixture();await f.g.prepare('/shaders');f.library.glob2_webgpu_attach();f.g.size(16,16);
  const mesh={vertices:f.g.buffer(new Uint8Array(128),f.context.GPUBufferUsage.VERTEX),indices:f.g.buffer(new Uint8Array(24),f.context.GPUBufferUsage.INDEX)};
  f.g.meshes.set(7,mesh);
  const records=primitiveRecords(4),v=new DataView(records.buffer);
  for(let i=0;i<4;i++){v.setUint32(i*256+252,7,true);v.setUint32(i*256+44,3,true);v.setUint32(i*256+52,3,true);}
  v.setUint32(2*256+40,12,true); // different index range in the same physical mesh
  v.setUint32(3*256+32,32,true); // return index range, change vertex bounds
  f.g.submit(records,4,new Uint8Array(),new Uint8Array(),16,16);
  assert.equal(f.calls.passes.length,1);assert.equal(f.calls.vertex,2);assert.equal(f.calls.index,3);
  assert.equal(f.calls.bindings,4);assert.deepEqual(f.calls.draws,[[3],[3],[3],[3]]);
  assert.deepEqual(f.calls.sequence,['begin','indexed','indexed','indexed','indexed','end']);
});
