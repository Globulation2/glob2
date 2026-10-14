const {chromium}=require('/home/bradley/glob2-lean-candidate/browser/node_modules/@playwright/test');
const fs=require('node:fs'),path=require('node:path');
const root='/home/bradley/glob2-lean-candidate/artifacts/lean-compute/web';
const evidence='/home/bradley/glob2-lean-candidate/artifacts/lean-compute/evidence';
(async()=>{
 const browser=await chromium.launch({args:['--enable-unsafe-webgpu','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 try{
  for(const variant of ['serial','threaded']){
   const log=path.join(evidence,`browser-${variant}-lifecycle.log`);fs.writeFileSync(log,'');
   const page=await browser.newPage();
   page.on('console',message=>fs.appendFileSync(log,message.text()+'\n'));
   page.on('pageerror',error=>fs.appendFileSync(log,String(error)+'\n'));
   const name=`lean-${variant}-lifecycle.html`,prefix=variant==='threaded'?'/threaded/':'/';
   fs.writeFileSync(path.join(root,name),`<canvas id="canvas"></canvas><script>
    var Module={noInitialRun:true,canvas:document.querySelector('canvas'),locateFile:name=>name.endsWith('.data')?'/'+name:'${prefix}'+name,
    async onRuntimeInitialized(){window.heartbeats=0;const timer=setInterval(()=>++window.heartbeats,10);
      window.result=await Module.start(['--test-suite=EntityRandom,EntityRandomLifecycle,ComputeExecutor,ApplicationResourcePlan,ReplayTelemetry,GradientPipeline,SharedWorkerLifecycle,SimulationReadPhase,WorldSnapshot,BuildingGradientInvalidation,PathGradient','--test-case-exclude=*[display*']);clearInterval(timer);}};
    </script><script src="${prefix}script-tests.js"></script>`);
   try{
    await page.goto('http://127.0.0.1:8791/'+name);
    await page.waitForFunction(()=>window.result!==undefined,null,{timeout:300000});
    const result=await page.evaluate(()=>({code:window.result,heartbeats:window.heartbeats}));
    fs.appendFileSync(log,JSON.stringify(result)+'\n');console.log(variant,JSON.stringify(result));
    if(result.code!==0 || (variant==='threaded' && !result.heartbeats))throw Error('Failed '+variant+' lifecycle: '+log);
   }finally{fs.unlinkSync(path.join(root,name));await page.close();}
  }
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
