const {test}=require('@playwright/test');
const fs=require('node:fs/promises');
test.afterEach(async({browser,page},info)=>{
 if(info.status===info.expectedStatus || browser.browserType().name()!=='chromium')return;
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
