const fs=require('node:fs');const path=require('node:path');const assert=require('node:assert/strict');
const root=process.cwd();
const playwright=require(path.join(root,'browser/node_modules/playwright'));
const {compiledSuiteInventory,compareCaseInventory}=require(path.join(root,'browser/tests/compiled-case-inventory'));
(async()=>{
 for(const name of ['firefox','webkit']){
  const browser=await playwright[name].launch();
  try {
   const page=await browser.newPage({baseURL:'http://127.0.0.1:8779'});
   const directory=path.join(root,'artifacts/ci-watch/2103/inventory-proof',name);fs.mkdirSync(directory,{recursive:true});
   const hosted=path.join(root,'artifacts/ci-watch/2103',name==='firefox'?'hosted-firefox':'hosted-webkit-gradient','gradient-publication/serial',name,'tests.xml');
   const junit=fs.readFileSync(hosted,'utf8');
   for(const variant of ['serial','threaded']){
    const listing=await compiledSuiteInventory(page,variant,'DelayedGradientPublication');
    fs.writeFileSync(path.join(directory,variant+'-selected.xml'),listing);
    const inventory=await compareCaseInventory(page,listing,junit);
    assert.equal(inventory.selected.length,26);
    fs.writeFileSync(path.join(directory,variant+'-comparison.json'),JSON.stringify(inventory,null,2));
    console.log(name,variant,'PASS exact 26-case registry/hosted execution inventory');
    for(const [mutation,xml] of [['missing',junit.replace(/<testcase\b[^>]*\/>/,'')],['extra',junit.replace('</testsuite>','<testcase name="unexpected"/></testsuite>')],['malformed','<testsuites>']]){
     await assert.rejects(()=>compareCaseInventory(page,listing,xml));console.log(name,variant,'rejects',mutation);
    }
   }
  }finally{await browser.close();}
 }
})().catch(e=>{console.error(e);process.exitCode=1;});
