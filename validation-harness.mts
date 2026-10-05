import { createAiValidator } from '../../platform/apps/engine-agent/src/aiValidation.ts';
import { DEFAULT_LIMITS } from '../../platform/packages/engine/src/engine.ts';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
const root=resolve(import.meta.dirname,'../..'), binary=resolve(root,'build/linux/client/release/src/glob2');
const sim=JSON.parse(execFileSync(binary,['--sim-version'],{cwd:root,encoding:'utf8'}));
const validate=await createAiValidator({binary,workdir:root,scratchRoot:resolve(root,'artifacts/ai-library/scratch'),limits:DEFAULT_LIMITS,maxOutputBytes:64*1024*1024},sim);
for(const [name,source] of Object.entries({profile1:'function step(ctx) {}',profile2:'export function metadata(){return {apiVersion:2,name:"Patient",version:"1"};} export function step(ctx) {}',unsupported:'function metadata(){return {apiVersion:99,name:"Unsupported"}} function step(){}',startup:'throw new Error("Startup failed"); function step(){}',syntax:'function step( {',callback:'var n=1;',state:'var bad = new (class Example {})(); function step() {}',budget:'function step(){while(true){}}',order:'function step(){return {type:"invalid"};}'})) {
 const report=await validate(Buffer.from(source),new AbortController().signal,r=>{console.log(name,r.checks.map(c=>c.id+':'+c.status).join(' '));return Promise.resolve();});
 console.log(name,JSON.stringify(report));writeFileSync(resolve(root,'artifacts/ai-library/'+name+'-report.json'),JSON.stringify(report,null,2));
}
