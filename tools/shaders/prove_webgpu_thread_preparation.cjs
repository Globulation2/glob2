// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK pthread: main owns the preload; worker owns compiled GPU pipelines.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({args:['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-vulkan=swiftshader','--use-angle=vulkan','--disable-vulkan-surface']});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.goto(process.argv[2]||'http://127.0.0.1:8792/');await page.waitForFunction(()=>window.proofReady);
  assert.equal(await page.evaluate(()=>crossOriginIsolated),true);
  assert.equal(await page.evaluate(()=>Module._proof_thread_prepare()),1);
  await page.waitForFunction(()=>Module._proof_thread_status()!==0&&Module._proof_thread_status()!==1,{},{timeout:60000});
  const result=await page.evaluate(()=>({status:Module._proof_thread_status(),workerOwned:Module._proof_thread_owner(),workerLocalMetadata:Module._proof_thread_local_metadata(),mainMetadata:FS.readFile('/shaders/metadata.json').length,mainGpuDevice:Glob2WebGPU.device!==null}));
  assert.deepEqual(errors,[]);assert.equal(result.status,2);assert.equal(result.workerOwned,1);assert.equal(result.workerLocalMetadata,0);assert(result.mainMetadata>0);assert.equal(result.mainGpuDevice,false);
  console.log(JSON.stringify(result,null,2));
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
