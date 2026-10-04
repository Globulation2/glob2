const {test}=require('@playwright/test');
const fs=require('node:fs/promises');
test.beforeEach(async({context})=>{await context.addInitScript({path:require('node:path').join(__dirname,'browser-proxy-arguments-init.js')});});
test.afterEach(async({browser,page},info)=>{
 if(info.status===info.expectedStatus)return;
 const observations=[];for(const open of page.context().pages())observations.push(await open.evaluate(()=>({url:location.href,snapshot:glob2Diagnostics?.snapshot(),logs:Module?.browserLog,proxyCalls:glob2ProxyCalls,invalidProxyCalls:glob2InvalidProxyCalls})).catch(error=>({error:String(error)})));
 await fs.writeFile(info.outputPath('proxy-arguments.json'),JSON.stringify(observations,null,2));
 if(browser.browserType().name()!=='chromium')return;
 const records=[];let root;
 try{
  root=await browser.newBrowserCDPSession();
  const sessions=[];
  root.on('Target.receivedMessageFromTarget',event=>records.push(event));
  const targets=await root.send('Target.getTargets');records.push(targets);
  for(const target of targets.targetInfos.filter(t=>t.type==='worker')){
   try{
    const {sessionId}=await root.send('Target.attachToTarget',{targetId:target.targetId,flatten:false});sessions.push(sessionId);
    await root.send('Target.sendMessageToTarget',{sessionId,message:JSON.stringify({id:1,method:'Debugger.enable'})});
    await root.send('Target.sendMessageToTarget',{sessionId,message:JSON.stringify({id:2,method:'Debugger.pause'})});
   }catch(error){records.push({error:String(error),target});}
  }
  await new Promise(resolve=>setTimeout(resolve,3000));
  const state=await page.evaluate(()=>({snapshot:glob2Diagnostics.snapshot(),logs:Module.browserLog}));records.push(state);
  await fs.writeFile(info.outputPath('blocked-worker-stacks.json'),JSON.stringify(records,null,2));
  for(const sessionId of sessions)try{await root.send('Target.sendMessageToTarget',{sessionId,message:JSON.stringify({id:3,method:'Debugger.resume'})});}catch{}
 }catch(error){await fs.writeFile(info.outputPath('diagnostics-error.txt'),String(error));}
 finally{await root?.detach().catch(()=>{});}
});
