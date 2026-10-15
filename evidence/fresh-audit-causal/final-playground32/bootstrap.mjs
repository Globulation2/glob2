// Reproduce both control-isolate bootstrap computations. Run only when appropriate.
import {readFileSync, writeFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {dirname, join} from 'node:path';
const folder=dirname(fileURLToPath(import.meta.url));
const input=JSON.parse(readFileSync(join(folder,'bootstrap-input.json'),'utf8'));
function calculate(algorithm, seed) {
 let state=seed>>>0;
 function random() {
  if(algorithm==='xorshift32') {state^=state<<13;state^=state>>>17;state^=state<<5;return(state>>>0)/4294967296;}
  let t=state+=0x6D2B79F5;t=Math.imul(t^t>>>15,t|1);t^=t+Math.imul(t^t>>>7,t|61);return((t^t>>>14)>>>0)/4294967296;
 }
 const keys=input.keys,n=input.pairs.length,values=Object.fromEntries(keys.map(k=>[k,[]]));
 for(let d=0;d<10000;d++) {
  const b=Array(keys.length).fill(0),w=Array(keys.length).fill(0);
  for(let j=0;j<n;j++) {const p=input.pairs[Math.floor(random()*n)];keys.forEach((k,z)=>{b[z]+=p.baseline[k];w[z]+=p['wake-only'][k];});}
  keys.forEach((k,z)=>{if(b[z])values[k].push(100*(w[z]/b[z]-1));});
 }
 function quantile(a,p){const v=(a.length-1)*p,i=Math.floor(v);return a[i]+(a[Math.min(i+1,a.length-1)]-a[i])*(v-i);}
 const metrics={};for(const k of keys){values[k].sort((a,b)=>a-b);metrics[k]={paired_seed_MC10000_percentile95_percent:[quantile(values[k],.025),quantile(values[k],.975)],valid_draws:values[k].length};}
 return{implementation:algorithm,seed,draws:10000,unit:'32pairedseeds, sameindicesacrossmetrics',metrics};
}
writeFileSync(join(folder,'bootstrap.json'),JSON.stringify(calculate('xorshift32',20261015),null,2)+'\n');
writeFileSync(join(folder,'bootstrap-independent-prng.json'),JSON.stringify(calculate('mulberry32',1729),null,2)+'\n');
