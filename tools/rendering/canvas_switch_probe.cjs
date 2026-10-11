#!/usr/bin/env node
// Run with the repository-pinned browser test dependency installed; no game build required.
const fs=require('node:fs');
const path=require('node:path');
const http=require('node:http');
const {createRequire}=require('node:module');
const requireBrowser=createRequire(path.resolve(__dirname,'../../browser/package.json'));
const {chromium,firefox,webkit}=requireBrowser('@playwright/test');
(async()=>{
  const browserName=process.argv[2]||'chromium';
  const output=process.argv[3]||'artifacts/renderer-feasibility/canvas-'+browserName+'.json';
  const engines={chromium,firefox,webkit};
  if(!engines[browserName])throw new Error('Expected chromium, firefox, or webkit');
  const html=fs.readFileSync(path.join(__dirname,'canvas_switch_probe.html'));
  const server=http.createServer((request,response)=>{
    response.writeHead(200,{'Content-Type':'text/html','Cross-Origin-Opener-Policy':'same-origin',
      'Cross-Origin-Embedder-Policy':'require-corp'});response.end(html);
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  const report={browser:browserName,proof:'context replacement with persistent Wasm instance',results:[]};
  try {
    browser=await engines[browserName].launch();report.version=browser.version();
    const page=await browser.newPage();await page.goto('http://127.0.0.1:'+server.address().port);
    for(const threaded of [false,true]) {
      try {report.results.push(await page.evaluate(threaded=>probeCanvasSwitch(threaded),threaded));}
      catch(error){report.results.push({threaded,passed:false,error:String(error)});}
    }
  } finally {
    if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));
    fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
  }
  console.log(JSON.stringify(report));if(report.results.length!==2||report.results.some(r=>!r.passed))process.exitCode=1;
})().catch(error=>{console.error(error);process.exitCode=1;});
