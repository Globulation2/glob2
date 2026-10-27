// Replay the unchanged corpus capture against its retained, identified producer.
// This does not replace or bypass the committed test's current-source guard.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=process.cwd();const pw=require(path.join(root,'browser/node_modules/playwright'));
const {compiledSuiteInventory,compareCaseInventory}=require(path.join(root,'browser/tests/compiled-case-inventory'));
const {openRuntimeHost}=require(path.join(root,'browser/tests/runtime-host'));
const text=fs.readFileSync(path.join(root,'browser/tests/determinism.spec.js'),'utf8');
const section=text.slice(text.indexOf("test('WebAssembly verifies ground gradient publication"));
const capture=section.slice(section.indexOf('    await openRuntimeHost(page,'),section.indexOf('    const source = JSON.parse'));
const AsyncFunction=Object.getPrototypeOf(async function(){}).constructor;
const run=new AsyncFunction('page','variant','openRuntimeHost','fs','path','output','progress',capture+'\nreturn result;');
(async()=>{
 for(const name of ['firefox','webkit']){
  const browser=await pw[name].launch();let serialTraces;
  try{
   const page=await browser.newPage({baseURL:'http://127.0.0.1:8779'});
   for(const variant of ['serial','threaded']){
    const output=path.join(root,'artifacts/ci-watch/2103/retained-corpus',name,variant);fs.mkdirSync(output,{recursive:true});
    const listing=await compiledSuiteInventory(page,variant,'DelayedGradientPublication');
    fs.writeFileSync(path.join(output,'compiled-case-inventory.xml'),listing);
    const result=await run(page,variant,openRuntimeHost,fs,path,output,[]);
    assert.equal(result.error,undefined);assert.equal(result.exit,0);assert.equal(result.threaded,variant==='threaded');
    const producer=JSON.parse(result.files['cases/build-provenance.json']);
    assert.equal(producer.revision,'697ad9d29ed4c2c7d5c965c71dd4ad54395e8306');assert.equal(producer.dirty,false);
    const junit=result.files['tests.xml'];assert.match(junit,/failures="0"/);assert.match(junit,/errors="0"/);assert.doesNotMatch(junit,/<skipped\b/);
    const inventory=await compareCaseInventory(page,listing,junit);assert.equal(inventory.selected.length,26);
    for(const n of [1,2,4,8])for(const suffix of ['.tsv','-kernel-entries.tsv'])assert(Object.keys(result.files).some(p=>p.endsWith('/participants-'+n+suffix)));
    const traces=Object.fromEntries(Object.entries(result.files).filter(([n])=>n.endsWith('.tsv')&&!n.endsWith('-kernel-entries.tsv')));
    assert.equal(Object.keys(traces).length,10);
    for(const [n,t] of Object.entries(traces)){
     const rows=t.trimEnd().split('\n');assert.match(rows[0],/^boundary\t/);assert.equal(rows.length,n.endsWith('-continuation.tsv')?34:49);
    }
    if(variant==='serial')serialTraces=traces;else assert.deepEqual(traces,serialTraces);
    fs.writeFileSync(path.join(output,'validation.json'),JSON.stringify({producer,variant,browser:name,browserVersion:browser.version(),cases:inventory.selected.length,traces:Object.keys(traces),result:'pass'},null,2));
    console.log(name,variant,'PASS 26 cases, 10 complete boundary traces and participant kernels');
   }
  }finally{await browser.close();}
 }
})().catch(e=>{console.error(e);process.exitCode=1;});
