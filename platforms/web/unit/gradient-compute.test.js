// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test'),assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
function fixture(navigator={}){
  const entries={},memory=new SharedArrayBuffer(4096),heap=new Int32Array(memory);let now=0;
  const context=vm.createContext({navigator,HEAP32:heap,HEAPU32:new Uint32Array(memory),Atomics,Uint32Array,Promise,console:{error(){}},addToLibrary:e=>Object.assign(entries,e),UTF8ToString:()=>'',performance:{now:()=>++now},GPUBufferUsage:{STORAGE:1,COPY_DST:2,COPY_SRC:4,MAP_READ:8},GPUMapMode:{READ:1}});
  vm.runInContext(fs.readFileSync(path.join(__dirname,'../gradient-compute.js'),'utf8'),context);context.Glob2GradientGPU=entries.$Glob2GradientGPU;
  return{g:context.Glob2GradientGPU,entries,heap};
}
test('missing WebGPU resolves initialization failure without retaining a device',async()=>{
  const f=fixture();f.entries.glob2_gradient_prepare(0,0,0);await new Promise(resolve=>setImmediate(resolve));assert.equal(Atomics.load(f.heap,0),-1);assert.equal(f.g.device,null);
});
test('completion is atomically published after owned async execution',async()=>{
  const f=fixture();let complete;f.g.execute=()=>new Promise(resolve=>{complete=resolve;});
  f.entries.glob2_gradient_execute(0,0,0,0,1,1,10,0);assert.equal(f.heap[0],0);complete(true);await Promise.resolve();assert.equal(f.heap[0],1);
});
test('rejection and cancellation resolve exactly one failure completion',async()=>{
  for(const result of[false,Error('lost')]){const f=fixture();f.g.execute=()=>result instanceof Error?Promise.reject(result):Promise.resolve(result);
    f.entries.glob2_gradient_execute(0,0,0,0,1,1,10,0);await Promise.resolve();assert.equal(f.heap[0],result===false?-2:-1);}
});
test('shutdown destroys only the compute device',()=>{
  const f=fixture();let destroyed=0;f.g.device={destroy(){destroyed++;}};f.entries.glob2_gradient_shutdown();assert.equal(destroyed,1);assert.equal(f.g.device,null);
});

function initializationDevice(overrides={}) {
  let destroyed=0;
  const device={
    lost:new Promise(()=>{}),
    createShaderModule(){return{getCompilationInfo:async()=>({messages:[]})};},
    createComputePipelineAsync:async()=>({}),
    destroy(){destroyed++;},
    ...overrides
  };
  return{device,destroyed:()=>destroyed};
}
test('initialization pipeline rejection destroys unpublished device',async()=>{
  const d=initializationDevice({createComputePipelineAsync:async()=>{throw Error('pipeline rejected');}});
  const f=fixture({gpu:{requestAdapter:async()=>({requestDevice:async()=>d.device})}});
  await assert.rejects(f.g.prepare('frozen','jacobi'),/pipeline rejected/);
  assert.equal(d.destroyed(),1);assert.equal(f.g.device,null);assert.equal(f.g.pipelines,null);
});
test('initialization second shader failure leaves no partially usable generation',async()=>{
  let shaders=0;
  const d=initializationDevice({createShaderModule(){return{getCompilationInfo:async()=>({messages:++shaders===2?[{type:'error',message:'bad jacobi'}]:[]})};}});
  const f=fixture({gpu:{requestAdapter:async()=>({requestDevice:async()=>d.device})}});
  await assert.rejects(f.g.prepare('frozen','jacobi'),/bad jacobi/);
  assert.equal(d.destroyed(),1);assert.equal(f.g.device,null);
});
test('device loss during asynchronous initialization retires unpublished device',async()=>{
  const d=initializationDevice({lost:Promise.resolve({message:'removed'}),createShaderModule(){return{getCompilationInfo:()=>new Promise(()=>{})};}});
  const f=fixture({gpu:{requestAdapter:async()=>({requestDevice:async()=>d.device})}});
  await assert.rejects(f.g.prepare('frozen','jacobi'),/device lost: removed/);
  assert.equal(d.destroyed(),1);assert.equal(f.g.device,null);
});
test('successful initialization is published atomically and reused',async()=>{
  const d=initializationDevice();let requests=0;
  const f=fixture({gpu:{requestAdapter:async()=>{requests++;return{info:{vendor:'test'},requestDevice:async()=>d.device};}}});
  await f.g.prepare('frozen','jacobi');await f.g.prepare('frozen','jacobi');
  assert.equal(requests,1);assert.equal(f.g.device,d.device);assert.equal(f.g.pipelines.length,2);assert.equal(d.destroyed(),0);
  f.entries.glob2_gradient_shutdown();assert.equal(d.destroyed(),1);assert.equal(f.g.lost,null);assert.equal(f.g.adapterInfo,null);
});

test('canceled execution destroys all buffers even when error-scope read rejects',async()=>{
  const f=fixture();let destroyed=0,retired=0;
  f.g.device={
    limits:{maxStorageBufferBindingSize:1024,maxComputeWorkgroupsPerDimension:1024},
    pushErrorScope(){},popErrorScope:async()=>{throw Error('scope failure');},
    createBuffer(){return{destroy(){destroyed++;}};},
    createBindGroup(){return{};},
    queue:{writeBuffer(){},async onSubmittedWorkDone(){retired++;}}
  };
  f.g.pipelines=[{getBindGroupLayout(){return{};}},{getBindGroupLayout(){return{};}}];
  f.heap[1]=1;
  await assert.rejects(f.g.execute(0,0,0,0,1,1,10,false),/scope failure/);
  assert.equal(retired,1);assert.equal(destroyed,8);
});

test('canceled completion retains signal and buffers until queue retirement',async()=>{
  const f=fixture();let retire,destroyed=0;
  f.g.device={
    limits:{maxStorageBufferBindingSize:1024,maxComputeWorkgroupsPerDimension:1024},
    pushErrorScope(){},popErrorScope:async()=>null,
    createBuffer(){return{destroy(){destroyed++;}};},createBindGroup(){return{};},
    queue:{writeBuffer(){},onSubmittedWorkDone:()=>new Promise(resolve=>{retire=resolve;})}
  };
  f.g.pipelines=[{getBindGroupLayout(){return{};}},{getBindGroupLayout(){return{};}}];
  f.heap[1]=1;
  f.heap[4]=0x12345678; // Canary immediately after the four-word task signal.
  f.entries.glob2_gradient_execute(0,0,0,0,1,1,10,false);
  await Promise.resolve();
  assert.equal(f.heap[0],0);assert.equal(destroyed,0);
  retire();await new Promise(resolve=>setImmediate(resolve));
  assert.equal(f.heap[0],-2);assert.equal(destroyed,8);
  assert.equal(f.heap[2],0);assert.equal(f.heap[3],0); // Canceled before any dispatch.
  assert.equal(f.heap[4],0x12345678);
});
