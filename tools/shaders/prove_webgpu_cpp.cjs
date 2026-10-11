// SPDX-License-Identifier: GPL-3.0-or-later
// Run against WebGPUCppProof.cpp compiled with the repository's pinned SDK/SDL.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({args:['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-vulkan=swiftshader','--use-angle=vulkan','--disable-vulkan-surface']});
 try {
  const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.goto(process.argv[2]||'http://127.0.0.1:8785/');await page.waitForFunction(()=>window.proofReady);
  const result=await page.evaluate(async()=>{
   const g=Glob2WebGPU;await g.prepare('/shaders/webgpu');if(g.state!==1)throw Error(g.error);
   function call(name,...args){if(!Module['_proof_'+name](...args))throw Error(UTF8ToString(Module._proof_error()));}
   async function wait(){await g.device.queue.onSubmittedWorkDone();await new Promise(r=>setTimeout(r,0));}
   async function pixels(){await wait();if(!g.capture(999,64,64))throw Error('Capture rejected');const item=g.captures.at(-1);while(!item.ready)await new Promise(r=>setTimeout(r,1));const mapped=new Uint8Array(item.buffer.getMappedRange()).slice();item.buffer.unmap();item.buffer.destroy();g.captures.pop();return(x,y)=>Array.from(mapped.slice(y*item.pitch+x*4,y*item.pitch+x*4+4));}
   call('init');const skin=[];
   for(let q=0;q<4;q++){call('skin',q);const p=await pixels();skin.push([p(20,20),p(44,44)]);}
   const targets=[];for(let pma=0;pma<2;pma++){call('terrain',pma);targets.push((await pixels())(32,32));}
   call('scissor');const seamPixels=await pixels();const scissor=[seamPixels(0,0),seamPixels(31,32),seamPixels(32,32),seamPixels(63,63)];
   call('pressure',0);await wait();call('pressure',1);await wait();call('pressure',2);
   const leased=Array.from(g.resources.keys());call('trim');const protectedKeys=Array.from(g.resources.keys());call('pressure',3);const p=await pixels();
   const pressure=[p(16,32),p(48,32)],remaining=Array.from(g.resources.keys());call('trim');const trimmed=Array.from(g.resources.keys());call('pressure',1);const restored=(await pixels())(32,32);
   call('pressure',4);const recreatedWithPending=Module._proof_pending_recreation();await wait();call('trim');const preparedTrimmed=Array.from(g.resources.keys());call('pressure',5);const afterPreparedFailure=(await pixels())(32,32);
   return{skin,targets,scissor,leased,pressure,remaining,protectedKeys,trimmed,restored,preparedTrimmed,afterPreparedFailure,recreatedWithPending,gpuError:g.error};
  });
  assert.deepEqual(errors,[]);assert.equal(result.gpuError,'');
  result.skin.forEach((samples,q)=>{assert.equal(samples[0][3],255);assert(samples[0].slice(0,3).some(v=>v>40),`quadrant ${q} rendered`);assert.notDeepEqual(samples[0],samples[1],`quadrant ${q} non-solid paint`);});
  assert.equal(new Set(result.skin.map(s=>s[0].slice(0,3).join(','))).size,4);
  assert.equal(result.recreatedWithPending,1);assert.deepEqual(result.preparedTrimmed,[]);assert.deepEqual(result.afterPreparedFailure,[20,180,30,255]);
  assert.deepEqual(result.scissor,Array(4).fill([100,45,20,255]));
  assert.deepEqual(result.targets,[[0,128,0,255],[0,128,0,255]]);
  assert.equal(result.leased.length,2);assert.deepEqual(result.protectedKeys,[result.leased[1]]);assert.equal(result.trimmed.length,0);assert.deepEqual(result.restored,[180,30,20,255]);assert.deepEqual(result.remaining,[result.leased[1],result.leased[1]+1]);assert.deepEqual(result.pressure,[[180,30,20,255],[20,180,30,255]]);
  console.log(JSON.stringify(result,null,2));
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
