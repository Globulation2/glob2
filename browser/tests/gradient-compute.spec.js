// SPDX-License-Identifier: GPL-3.0-or-later
const {test,expect,chromium}=require('@playwright/test');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'../..');
const library=fs.readFileSync(path.join(root,'browser/gradient-compute.js'),'utf8');
const shaders=['frozen8','jacobi'].map(name=>fs.readFileSync(path.join(root,'src/field/shaders',name+'.wgsl'),'utf8'));
test('WebGPU compute algorithms match exact weighted fields and preserve canceled output',async({browserName})=>{
  test.skip(browserName!=='chromium','WebGPU compute qualification uses Chromium with explicit compute support');
  const browser=await chromium.launch({executablePath:process.env.PLAYWRIGHT_CHROMIUM,args:['--enable-unsafe-webgpu','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
  try{
    const page=await browser.newPage();
    await page.route('**/gradient-proof',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>Gradient compute proof</title>',headers:{'Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp'}}));
    await page.goto('http://127.0.0.1:8770/gradient-proof');
    const result=await page.evaluate(async({library,shaders})=>{
      const entries={};globalThis.addToLibrary=value=>Object.assign(entries,value);eval(library);globalThis.Glob2GradientGPU=entries.$Glob2GradientGPU;
      const memory=new SharedArrayBuffer(1024*1024);globalThis.HEAP32=new Int32Array(memory);globalThis.HEAPU32=new Uint32Array(memory);
      const g=Glob2GradientGPU;await g.prepare(...shaders);let cases=0;
      const wrap=(x,n)=>(x%n+n)%n;
      function reference(seeds,costs,w,h,cap){const out=seeds.slice();let changes=true;
        while(changes){changes=false;for(let i=0;i<out.length;i++)if(out[i]){
          let value=out[i];const x=i%w,y=Math.floor(i/w);
          for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++)if(dx||dy){const k=wrap(y+dy,h)*w+wrap(x+dx,w),step=dx&&dy?costs[k]>>>16:costs[k]&65535,candidate=out[k]-step;if(candidate>=Math.max(2,65535-cap))value=Math.max(value,candidate);}
          if(value!==out[i]){out[i]=value;changes=true;}
        }}return out;}
      for(const frozen of[0,1])for(const[w,h]of[[1,1],[1,32],[32,1],[2,16],[17,19],[32,32]])for(const cap of[0,70,65500]){
        const n=w*h,seeds=Array(n).fill(1),costs=Array.from({length:n},(_,i)=>{const c=1+(i*71)%181;return c|(Math.floor(c*14/10)<<16);});
        for(let i=7;i<n;i+=11)seeds[i]=0;seeds[0]=65535;seeds[n-1]=65135;
        HEAP32.fill(0,0,3);HEAPU32.set(seeds,1024);HEAPU32.set(costs,8192);HEAPU32.fill(999,16384,16384+n);
        if(!await g.execute(0,4096,32768,65536,w,h,cap,frozen))throw Error('Unexpected decline');
        const expected=reference(seeds,costs,w,h,cap),actual=Array.from(HEAPU32.slice(16384,16384+n));
        for(let i=0;i<n;i++)if(actual[i]!==expected[i])throw Error(`Mismatch ${frozen} ${w}x${h} cap=${cap} at ${i}: ${actual[i]} != ${expected[i]}`);
        cases++;
      }
      HEAP32[1]=1;HEAPU32[16384]=999;
      if(await g.execute(0,4096,32768,65536,1,1,65500,0))throw Error('Cancellation accepted');
      if(HEAPU32[16384]!==999)throw Error('Canceled output mutated');
      g.finish(0,1);if(Atomics.load(HEAP32,0)!==1)throw Error('Completion publication failed');
      g.device.destroy();return{cases};
    },{library,shaders});
    expect(result.cases).toBe(36);
  }finally{await browser.close();}
});
