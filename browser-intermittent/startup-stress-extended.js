const {chromium}=require('../../browser/node_modules/playwright');
const fs=require('fs');
(async()=>{
 const browser=await chromium.launch({args:['--remote-debugging-port=9228']});
 const version=await(await fetch('http://127.0.0.1:9228/json/version')).json();
 const ws=new WebSocket(version.webSocketDebuggerUrl);await new Promise(r=>ws.addEventListener('open',r,{once:true}));
 let seq=0;const pending=new Map(),events=[];
 ws.addEventListener('message',e=>{const d=JSON.parse(e.data);if(d.id){pending.get(d.id)?.(d);pending.delete(d.id);}else events.push(d);});
 const send=(method,params={},sessionId)=>new Promise(r=>{const id=++seq;pending.set(id,r);ws.send(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})}));});
 for(let batch=0;batch<50;++batch){
  const contexts=await Promise.all(Array.from({length:8},()=>browser.newContext()));
  const pages=await Promise.all(contexts.map(c=>c.newPage()));
  const failures=[];
  await Promise.all(pages.map(async(p,i)=>{
   const logs=[];p.on('console',m=>logs.push(m.text()));p.on('pageerror',e=>logs.push(String(e)));
   await p.goto('http://127.0.0.1:8775/');
   try{await p.waitForFunction(()=>glob2Diagnostics.snapshot().screen.includes('MainMenuScreen'),{},{timeout:15000});}
   catch(e){failures.push({batch,i,state:await p.evaluate(()=>glob2Diagnostics.snapshot()),logs});}
  }));
  if(failures.length){
   const targets=await send('Target.getTargets');
   for(const t of targets.result.targetInfos.filter(t=>['worker','page'].includes(t.type))){
    const a=await send('Target.attachToTarget',{targetId:t.targetId,flatten:true});
    if(a.result){await send('Debugger.enable',{},a.result.sessionId);await send('Debugger.pause',{},a.result.sessionId);}
   }
   await new Promise(r=>setTimeout(r,1000));
   fs.writeFileSync('artifacts/ci-repair/startup-stress-extended-failure.json',JSON.stringify({failures,targets,events},null,2));
   console.log('FAILURE',batch,failures.map(f=>({i:f.i,state:f.state.screen,loop:f.state.loop})));break;
  }
  console.log('PASS',batch,'eight concurrent startups');await Promise.all(contexts.map(c=>c.close()));
 }
 ws.close();await browser.close();
})();
