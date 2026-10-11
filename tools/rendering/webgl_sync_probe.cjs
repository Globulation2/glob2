#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit pinned-toolchain regression: EMCXX points at the repository's SDK;
// PLAYWRIGHT_MODULE and CHROMIUM_EXECUTABLE select installed development tools.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'../..');
const output=path.resolve(process.argv[2]||path.join(root,'artifacts','webgl-sync-probe'));
async function run(){
  if(!process.env.EMCXX||!process.env.PLAYWRIGHT_MODULE)throw Error('Set EMCXX and PLAYWRIGHT_MODULE to the pinned SDK and installed Playwright module');
  fs.mkdirSync(output,{recursive:true});
  const built=spawnSync(process.env.EMCXX,[path.join(root,'browser/tests/fixtures/webgl-sync-probe.cpp'),'-O0','-sMIN_WEBGL_VERSION=2','-sMAX_WEBGL_VERSION=2','-sGL_ENABLE_GET_PROC_ADDRESS=1','-sEXIT_RUNTIME=0','-o',path.join(output,'probe.js')],{stdio:'inherit'});
  if(built.status!==0)throw Error('SDK probe compilation failed');
  fs.writeFileSync(path.join(output,'index.html'),'<canvas id="canvas" width="16" height="16"></canvas><script>var Module={canvas:document.getElementById("canvas"),onRuntimeInitialized(){globalThis.ready=true;}};</script><script src="probe.js"></script>');
  const server=http.createServer((req,res)=>{
    const name=req.url==='/'?'index.html':req.url.slice(1);
    if(!['index.html','probe.js','probe.wasm'].includes(name)){res.writeHead(404).end();return;}
    res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':name.endsWith('.js')?'text/javascript':'text/html');res.end(fs.readFileSync(path.join(output,name)));
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try{
    const {chromium}=require(process.env.PLAYWRIGHT_MODULE);
    browser=await chromium.launch({headless:true,executablePath:process.env.CHROMIUM_EXECUTABLE||undefined,args:['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
    const page=await browser.newPage();await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>globalThis.ready);
    const result=await page.evaluate(()=>{
      const direct=Module._probe(0);let raw;
      try{raw=Module._probe(1);}catch(error){raw=String(error);}
      return{direct,raw};
    });
    fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));
    if(![0x911a,0x911b,0x911c].includes(result.direct)||!String(result.raw).includes('signature mismatch'))throw Error('Pinned SDK fence ABI regression did not match expected typed-call success');
  }finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
}
run().catch(error=>{console.error(error);process.exitCode=1;});
