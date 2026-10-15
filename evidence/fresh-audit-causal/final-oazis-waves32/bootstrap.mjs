// Reproduction only; use control isolate while host timings run.
import{readFileSync,writeFileSync}from'node:fs';
const d=JSON.parse(readFileSync(new URL('./wave-input.json',import.meta.url)));let state=605442;
const values={wall:[],throughput:[],cpu:[],gamewall:[]};
for(let z=0;z<10000;z++){let b=0,w=0,bc=0,wc=0,bg=0,wg=0;
for(let i=0;i<8;i++){state^=state<<13;state^=state>>>17;state^=state<<5;const p=d.wave_pairs[(state>>>0)>>>29];b+=p.baseline.makespan_seconds;w+=p['wake-only'].makespan_seconds;
for(let j=0;j<4;j++){bc+=p.baseline.games[j].total_cpu_seconds;wc+=p['wake-only'].games[j].total_cpu_seconds;bg+=p.baseline.games[j].inner_seconds;wg+=p['wake-only'].games[j].inner_seconds;}}
values.wall.push(100*(w/b-1));values.throughput.push(100*(b/w-1));values.cpu.push(100*(wc/bc-1));values.gamewall.push(100*(wg/bg-1));}
function quantile(a,p){let t=(a.length-1)*p,i=Math.floor(t);return a[i]+(a[Math.min(i+1,a.length-1)]-a[i])*(t-i)}
const ci95_percent={};for(const[k,a]of Object.entries(values)){a.sort((x,y)=>x-y);ci95_percent[k]=[quantile(a,.025),quantile(a,.975)];}
writeFileSync(new URL('./bootstrap-reproduced.json',import.meta.url),JSON.stringify({seed:605442,algorithm:'xorshift32',draws:10000,ci95_percent},null,2));
