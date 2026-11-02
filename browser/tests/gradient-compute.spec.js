// SPDX-License-Identifier: GPL-3.0-or-later
const {test,expect,chromium}=require('@playwright/test');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'../..');
const library=fs.readFileSync(path.join(root,'browser/gradient-compute.js'),'utf8');
const shaders=['frozen8','jacobi','frozen8_destinations','jacobi_destinations'].map(name=>fs.readFileSync(path.join(root,'src/field/shaders',name+'.wgsl'),'utf8'));
const retainedSource=fs.readFileSync(path.join(root,'test/fixtures/gradients/RetainedVulkanSeeds.h'),'utf8');
const retained=[...retainedSource.matchAll(/retainedVulkanRuns\d+\{([\s\S]*?)\};/g)].map(match=>match[1].match(/\d+/g).map(Number));
if(retained.length!==5)throw Error('Expected all five retained NVIDIA fields');
const hardware=process.env.GLOB2_WEBGPU_EXPECT_DEVICE;
test('WebGPU compute algorithms match weighted costs and canonical destinations and preserve canceled output',async({browserName})=>{
  test.skip(browserName!=='chromium','WebGPU compute qualification uses Chromium with explicit compute support');
  const args=hardware
    ? ['--enable-unsafe-webgpu','--use-angle=vulkan','--enable-features=Vulkan','--use-vulkan=native','--disable-vulkan-surface']
    : ['--enable-unsafe-webgpu','--use-angle=swiftshader','--enable-unsafe-swiftshader'];
  const browser=await chromium.launch({executablePath:process.env.PLAYWRIGHT_CHROMIUM,args});
  try{
    const page=await browser.newPage();
    await page.route('**/gradient-proof',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>Gradient compute proof</title>',headers:{'Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp'}}));
    await page.goto('http://127.0.0.1:8770/gradient-proof');
    const result=await page.evaluate(async({library,shaders,retained,hardware})=>{
      const entries={};globalThis.addToLibrary=value=>Object.assign(entries,value);eval(library);globalThis.Glob2GradientGPU=entries.$Glob2GradientGPU;
      const memory=new SharedArrayBuffer(8*1024*1024);globalThis.HEAP32=new Int32Array(memory);globalThis.HEAPU32=new Uint32Array(memory);
      const g=Glob2GradientGPU;await g.prepare(...shaders);let cases=0;
      const info=g.adapterInfo,adapter={vendor:info.vendor,architecture:info.architecture,device:info.device,description:info.description};
      if(hardware && (!new RegExp(hardware,'i').test(JSON.stringify(adapter)) || /swiftshader|lavapipe|llvmpipe/i.test(JSON.stringify(adapter))))
        throw Error('Expected hardware '+hardware+', got '+JSON.stringify(adapter));
      const wrap=(x,n)=>(x%n+n)%n;
      function labeledReference(seeds,costs,w,h,cap){
       const out=seeds.slice(),labels=seeds.map((v,i)=>v>1?i:0xffffffff),threshold=Math.max(2,65535-cap);
       for(let round=0;round<65536;++round){let changed=false;
        for(let i=0;i<out.length;++i)if(out[i]){
         let value=out[i],source=labels[i];const x=i%w,y=Math.floor(i/w);
         for(let dy=-1;dy<=1;++dy)for(let dx=-1;dx<=1;++dx)if(dx||dy){const k=wrap(y+dy,h)*w+wrap(x+dx,w),candidate=out[k]-(dx&&dy?costs[k]>>>16:costs[k]&65535);
          if(candidate>=threshold&&(candidate>value||(candidate===value&&labels[k]<source))){value=candidate;source=labels[k];}}
         if(value!==out[i]||source!==labels[i]){changed=true;out[i]=value;labels[i]=source;}
        }
        if(!changed)return {out,labels};
       }throw Error('reference did not converge');
      }
      for(const frozen of[0,1])for(const[w,h]of[[1,1],[1,32],[32,1],[2,16],[17,19],[32,32]])for(const cap of[0,70,65500]){
        const n=w*h,seeds=Array(n).fill(1),costs=Array.from({length:n},(_,i)=>{const c=1+(i*71)%181;return c|(Math.floor(c*14/10)<<16);});
        for(let i=7;i<n;i+=11)seeds[i]=0;seeds[0]=65535;seeds[n-1]=65135;
        HEAP32.fill(0,0,4);HEAPU32.set(seeds,1024);HEAPU32.set(costs,32768);HEAPU32.fill(999,65536,65536+n);
        if(!await g.execute(0,4096,131072,262144,w,h,cap,frozen))throw Error('Unexpected decline');
        const expected=labeledReference(seeds,costs,w,h,cap).out,actual=Array.from(HEAPU32.slice(65536,65536+n));
        for(let i=0;i<n;i++)if(actual[i]!==expected[i])throw Error(`Mismatch ${frozen} ${w}x${h} cap=${cap} at ${i}: ${actual[i]} != ${expected[i]}`);
        cases++;
      }
      for(const runs of retained){
        const seeds=[];for(const run of runs)for(let n=run>>>2;n;n--)seeds.push((run&3)===2?65535:run&3);
        if(seeds.length!==128*128)throw Error('Malformed retained field');
        const costs=Array(seeds.length).fill(10|(14<<16)),expected=labeledReference(seeds,costs,128,128,65491).out;
        for(const frozen of[0,1])for(let repeat=0;repeat<3;repeat++){
          HEAP32.fill(0,0,4);HEAPU32.set(seeds,1024);HEAPU32.set(costs,32768);HEAPU32.fill(999,65536,65536+seeds.length);
          if(!await g.execute(0,4096,131072,262144,128,128,65491,frozen))throw Error('Retained device execution declined');
          for(let i=0;i<seeds.length;i++)if(HEAPU32[65536+i]!==expected[i])throw Error(`Retained mismatch ${frozen} repeat=${repeat} cell=${i}`);
          cases++;
        }
      }
      HEAP32[1]=1;HEAPU32[65536]=999;
      if(await g.execute(0,4096,131072,262144,1,1,65500,0))throw Error('Cancellation accepted');
      if(HEAPU32[65536]!==999)throw Error('Canceled output mutated');
      g.finish(0,1);if(Atomics.load(HEAP32,0)!==1)throw Error('Completion publication failed');
      let labeledCases=0;
      async function run(seeds,costs,w,h,cap,frozen){const n=w*h,expected=labeledReference(seeds,costs,w,h,cap);
       HEAP32.fill(0,0,4);const packed=seeds.flatMap((v,i)=>[v,v>1?i:0xffffffff]);HEAPU32.set(packed,1024);HEAPU32.set(costs,131072);HEAPU32.fill(999,262144,262144+2*n);
       if(!await g.execute(0,4096,524288,1048576,w,h,cap,frozen,true))throw Error('declined');
       for(let i=0;i<n;++i){const value=HEAPU32[262144+i*2],source=HEAPU32[262144+i*2+1];if(value!==expected.out[i]||source!==expected.labels[i])throw Error(JSON.stringify({frozen,w,h,cap,i,value,expectedValue:expected.out[i],source,expectedSource:expected.labels[i]}));}
       ++labeledCases;
      }
      for(const frozen of [false,true])for(const [w,h]of [[1,1],[1,32],[32,1],[2,16],[17,19],[32,32],[64,64]])for(const cap of[0,70,65500])for(const weighted of[false,true]){
       const n=w*h,seeds=Array(n).fill(1),costs=Array.from({length:n},(_,i)=>{const c=weighted?1+(i*71)%181:10;return c|(Math.floor(c*14/10)<<16);});
       for(let i=7;i<n;i+=11)seeds[i]=0;seeds[0]=65535;seeds[n-1]=65535;if(n>7)seeds[3]=65135;
       await run(seeds,costs,w,h,cap,frozen);
      }
      for(const runs of retained){const seeds=[];for(const run of runs)for(let n=run>>>2;n;n--)seeds.push((run&3)===2?65535:run&3);if(seeds.length!==16384)throw Error('bad retained');
       for(const frozen of[false,true])await run(seeds,Array(seeds.length).fill(10|(14<<16)),128,128,65491,frozen);
      }
      HEAP32[1]=1;HEAPU32.fill(999,262144,262146);if(await g.execute(0,4096,524288,1048576,1,1,65500,false,true))throw Error('cancellation accepted');
      if(HEAPU32[262144]!==999||HEAPU32[262145]!==999)throw Error('cancel mutated cost/source');
      g.device.destroy();return{cases,labeledCases,adapter};
    },{library,shaders,retained,hardware});
    console.log('WebGPU adapter:',JSON.stringify(result.adapter));
    expect(result.cases).toBe(66);
    expect(result.labeledCases).toBe(94);
  }finally{await browser.close();}
});

// Opt in when the compiled threaded harness is served by the browser test host.
test('WebGPU C++ worker ownership bridge requires real device commits',async({browserName})=>{
  test.skip(browserName!=='chromium' || process.env.GLOB2_WEBGPU_CPP!=='1','Requires the compiled threaded harness');
  test.setTimeout(120000);
  const {openRuntimeHost}=require('./runtime-host');
  const args=hardware
    ? ['--enable-unsafe-webgpu','--use-angle=vulkan','--enable-features=Vulkan','--use-vulkan=native','--disable-vulkan-surface']
    : ['--enable-unsafe-webgpu','--use-angle=swiftshader','--enable-unsafe-swiftshader'];
  const browser=await chromium.launch({executablePath:process.env.PLAYWRIGHT_CHROMIUM,args});
  try{
    const page=await browser.newPage({baseURL:process.env.GLOB2_TEST_URL || 'http://127.0.0.1:8770'});
    let output='';
    page.on('console',message=>{output+=message.text()+'\n';console.log(message.text());});
    await openRuntimeHost(page,`<canvas id="canvas"></canvas><script>
      var Module={noInitialRun:true,canvas:document.querySelector('canvas'),
        locateFile:name=>'/threaded/'+name,onAbort:reason=>window.failure=String(reason),
        async onRuntimeInitialized(){window.result=await Module.start(['--test-suite=WebGPUGradient']);}};
      </script><script src="/threaded/script-tests.js"></script>`);
    await page.waitForFunction(()=>window.result!==undefined||window.failure!==undefined,null,{timeout:110000});
    expect(await page.evaluate(()=>window.failure)).toBeUndefined();
    expect(await page.evaluate(()=>window.result)).toBe(0);
    const deviceLine=output.split('\n').find(line=>line.includes('WebGPU C++ device:')) || '';
    expect(deviceLine).toContain('WebGPU C++ device:');
    if(hardware) expect(deviceLine).toMatch(new RegExp(hardware,'i'));
  }finally{await browser.close();}
});
