const {test,expect}=require('@playwright/test');
const {spawnSync}=require('node:child_process');
const path=require('node:path');
const snapshot={team:0,tick:25,width:2,height:1,teams:[{id:0,alive:true},{id:1,alive:true}],units:[{id:1,generation:2,team:0,type:0,medical:1}],buildings:[],buildingTypes:[],tiles:[{x:0,y:0,visible:true,explored:true},{x:1,y:0,visible:false,explored:false}]};
test('Hive worker matches native results and rejects forbidden capabilities',async({page})=>{
 await page.route('**/hive-test.html',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>Hive worker contract</title>'}));
 await page.goto('/hive-test.html');
 const cases=[
  'let n=0;function step(ctx){return {output:{tick:ctx.tick,count:++n,own:ctx.game.units({team:ctx.myTeam}),hidden:ctx.game.units({team:1}),stale:ctx.game.unit({id:1,generation:1}),tile:ctx.game.map.tile(-1,0)}}}',
  'function step(ctx){ctx.wakeAgent({key:"ready",reason:"Colony ready",data:{tick:ctx.tick}});return {output:ctx.random()}}',
  'function step(ctx){return {output:ctx.game.objectives()}}',
  'function step(){return {output:fetch("https://example.com")}}',
  'function step(){while(true){}}',
 ];
 // Malformed snapshots/types exercise C++ exception recovery in the isolated
 // runtime as well as ordinary JavaScript failures and resource exhaustion.
 for(const input of [...cases.map(source=>({source,snapshot})),
  {source:cases[0],snapshot:{}},{source:42,snapshot}]){
  const web=await page.evaluate(input=>new Promise((resolve,reject)=>{
   const worker=new Worker('/hive-worker.js');const timer=setTimeout(()=>{worker.terminate();reject(new Error('Worker deadline'));},10000);
   worker.onmessage=e=>{clearTimeout(timer);worker.terminate();resolve(e.data)};
   worker.onerror=e=>{clearTimeout(timer);worker.terminate();reject(new Error(e.message))};
   worker.postMessage(JSON.stringify(input));
  }),input);
  const native=spawnSync(process.env.GLOB2_HIVE_NATIVE||path.resolve(__dirname,'../../build/linux/client/release/src/glob2'),['--hive-worker'],{input:JSON.stringify(input)+'\n',encoding:'utf8',timeout:10000});
  expect(native.status).toBe(0);
  const result=JSON.parse(native.stdout);
  expect(web.ok).toBe(result.ok);
  if(result.ok){expect(web.output).toEqual(result.output);expect(web.state).toEqual(result.state);expect(web.wakes).toEqual(result.wakes);}
 }
});
